#include <filesystem>
#include <iostream>
#include <vector>
#include <numeric>
#include <thread>
#include <exception>
#include <mz.h>
#include <mz_os.h>
#include <mz_strm.h>
#include <mz_strm_os.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>
#include <tclap/CmdLine.h>
#include <indicators/progress_bar.hpp>
#include <config.h>
#include "szkarc.h"

namespace fs = std::filesystem;
using std::cout;
using std::cerr;
using std::endl;
using std::flush;

void unzip(const fs::path& input, const fs::path& output)
{
  void* zip_reader;
  void* file_stream;
  mz_zip_reader_create(&zip_reader);
  mz_stream_os_create(&file_stream);

  int32_t err = stream_os_open(file_stream, input.string().c_str(), MZ_OPEN_MODE_READ);
  if (err != MZ_OK) {
    throw std::runtime_error("Failed to open a zip file:" + input.string());
  }
  err = mz_zip_reader_open(zip_reader, file_stream);
  if (err != MZ_OK) {
    throw std::runtime_error("Failed to open a zip file:" + input.string());
  }
#ifdef _WIN32
  auto utf8 = wstr2utf8(output.wstring());
  mz_zip_reader_save_all(zip_reader, utf8.c_str());
#else
  mz_zip_reader_save_all(zip_reader, output.c_str());
#endif

  err = mz_zip_reader_close(zip_reader);
  if (err != MZ_OK) {
    throw std::runtime_error("Failed to close a zip file:" + input.string());
  }
  err = mz_stream_os_close(file_stream);
  if (err != MZ_OK) {
    throw std::runtime_error("Failed to close a zip file:" + input.string());
  }
  mz_stream_os_delete(&file_stream);
  mz_zip_reader_delete(&zip_reader);
}

using PathList = std::vector<fs::path>;
PathList list_zipfiles(const fs::path& indir, int depth) {
  PathList list;
  if (depth > 0) {
    for (const auto& ent : fs::directory_iterator(indir)) {
      if (ent.is_directory()) {
        list.emplace_back(ent.path());
      }
    }
    std::sort(list.begin(), list.end());
    std::vector<PathList> nested;
    std::transform(list.cbegin(), list.cend(), std::back_inserter(nested), [depth](const fs::path& p) {
      return list_zipfiles(p, depth - 1);
      });
    auto flat = flatten_nested(nested);
    return flat;
  }
  else {
    for (const auto& ent : fs::directory_iterator(indir)) {
      if (ent.path().extension()==".zip") {
        list.emplace_back(ent.path());
      }
    }
    std::sort(list.begin(), list.end());
    return list;
  }
}

fs::path input2output(const fs::path &input_dir, const fs::path &output_dir, const fs::path &input) {
  auto relative = input.lexically_relative(input_dir);
  return (output_dir / relative.replace_extension("")).string();
}

int main(int argc, char* argv[])
{
  try {
    TCLAP::CmdLine cmd("Unzip all zip files in the input directory. version: " PROJECT_VERSION, ' ', PROJECT_VERSION);

    TCLAP::UnlabeledValueArg<std::string> a_input("input", "Input directory", true, "", "input", cmd);
    TCLAP::UnlabeledValueArg<std::string> a_output("output", "(optional) Output directory. <input> is used as <output> by default.", false, "", "output", cmd);
    TCLAP::ValueArg<int> a_depth("d", "depth", "(optional) Depth of the subdirectories.", false, 0, "int", cmd);
    TCLAP::ValueArg<int> a_jobs("j", "jobs", "(optional) Number of simultaneous jobs.", false, 0, "int", cmd);

    TCLAP::SwitchArg a_skip("", "skip", "Dont't unzip when the output directory exists.", cmd);
    TCLAP::SwitchArg a_dryrun("", "dryrun", "List zip files to unzip and exit.", cmd);
    cmd.parse(argc, argv);

    auto input_dir = fs::path(a_input.getValue());
    auto output_dir = fs::path(a_output.isSet() ? a_output.getValue() : a_input.getValue());
    auto depth = a_depth.getValue();
    auto jobs = a_jobs.getValue();
    auto zipfiles = list_zipfiles(input_dir, depth);
    if (a_skip.isSet()) {
      auto result = std::remove_if(zipfiles.begin(), zipfiles.end(), [&input_dir, &output_dir](auto& zf) {
        auto output = input2output(input_dir, output_dir, zf);
        return fs::exists(output);
        });
      auto orig_size = zipfiles.size();
      zipfiles.erase(result, zipfiles.end());
      cout << "Skip " << orig_size - zipfiles.size() << " existing entries." << endl;
    }
    if (zipfiles.empty()) {
      cout << "There is nothing to decompress." << endl;
      return 0;
    }
    if (a_dryrun.isSet()) {
      for (const auto& zf : zipfiles) {
        auto output = input2output(input_dir, output_dir, zf);
        cout << zf.string() << " -> " << output << '\n';
      }
      cout << flush;
      return 0;
    }
    using namespace indicators;
    ProgressBar bar{
       option::BarWidth{30},
       option::MaxProgress(zipfiles.size()),
       option::Start{"["},
       option::Fill{"="},
       option::Lead{">"},
       option::Remainder{" "},
       option::End{"]"},
       option::PrefixText{"Decompressing"},
       option::ShowElapsedTime{true},
       option::ShowRemainingTime{true},
    };
    if (jobs <= 0) {
      jobs = get_physical_core_counts();
      cout << "Using " << jobs << " CPU cores." << endl;
    }
    std::exception_ptr ep;
    std::mutex mtx_mkdir;
    std::vector<std::thread> threads;
    threads.reserve(jobs);
    for (int job_id = 0; job_id < jobs; ++job_id) {
      threads.emplace_back([job_id, jobs, &zipfiles, &input_dir, &output_dir, &bar, &mtx_mkdir, &ep]() {
        for (int i = job_id; i < zipfiles.size(); i += jobs) {
          const auto& zipfile = zipfiles[i];
          auto output = input2output(input_dir, output_dir, zipfile);
          {
            std::lock_guard<std::mutex> lock(mtx_mkdir);
            if (!fs::exists(output.parent_path())) {
              fs::create_directories(output.parent_path());
            }
          }
          try {
            unzip(zipfile, output);
          }
          catch (...) {
            ep = std::current_exception();
            break;
          }
          bar.tick();
        }
        });
    }
    for (auto& t : threads) {
      t.join();
    }
    if (ep) {
      std::rethrow_exception(ep);
    }
  }
  catch (TCLAP::ArgException& e)
  {
    std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
    return 1;
  }
  catch (std::exception& e) {
    cerr << e.what() << endl;
    return 1;
  }
  catch (const std::string& e) {
    cerr << e << endl;
    return 1;
  }
  return 0;
}
