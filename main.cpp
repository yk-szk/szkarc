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

void  zip_directory(const fs::path& input, const fs::path& output, int16_t level) {
  void* zip_writer;
  void* file_stream;
  int32_t err;
  mz_zip_writer_create(&zip_writer);
  mz_stream_os_create(&file_stream);
  mz_zip_writer_set_compress_method(zip_writer, MZ_COMPRESS_METHOD_DEFLATE);
  mz_zip_writer_set_compress_level(zip_writer, level);
  err = stream_os_open(file_stream, output, MZ_OPEN_MODE_WRITE | MZ_OPEN_MODE_CREATE);
  if (err != MZ_OK) {
    throw std::runtime_error("Failed to open a zip file:" + output.string());
  }
  err = mz_zip_writer_open(zip_writer, file_stream, 0);
  if (err != MZ_OK) {
    throw std::runtime_error("Failed to open a zip file:" + output.string());
  }

#ifdef _WIN32
  auto utf8 = wstr2utf8(input.wstring());
  err = mz_zip_writer_add_path(zip_writer, utf8.c_str(), NULL, 0, 1);
#else
  err = mz_zip_writer_add_path(zip_writer, input.string().c_str(), NULL, 0, 1);
#endif
  if (err != MZ_OK) {
    throw std::runtime_error("Failed to compress:" + input.string());
  }

  err = mz_zip_writer_close(zip_writer);
  if (err != MZ_OK) {
    throw std::runtime_error("Failed to close the zip writer:" + output.string());
  }
  mz_stream_os_close(file_stream);
  mz_stream_os_delete(&file_stream);
  mz_zip_writer_delete(&zip_writer);
};

fs::path input2output(const fs::path& input_dir, const fs::path& output_dir, const fs::path& input) {
  auto relative = input.lexically_relative(input_dir);
  auto output = (output_dir / relative).WSTRING() + WPREFIX(".zip");
  return output;
}

int main(int argc, char* argv[])
{
  try {
    TCLAP::CmdLine cmd("Zip each subdirectory. version: " PROJECT_VERSION, ' ', PROJECT_VERSION);

    TCLAP::UnlabeledValueArg<std::string> a_input("input", "Input directory", true, "", "input", cmd);
    TCLAP::UnlabeledValueArg<std::string> a_output("output", "(optional) Output directory. <input> is used as <output> by default.", false, "", "output", cmd);
    TCLAP::ValueArg<int> a_depth("d", "depth", "(optional) Depth of the subdirectories.", false, 0, "int", cmd);
    TCLAP::ValueArg<int> a_jobs("j", "jobs", "(optional) Number of simultaneous jobs.", false, 0, "int", cmd);
    TCLAP::ValueArg<int> a_level("l", "level", "(optional) Compression level. Default value is 1.", false, 1, "int", cmd);

    TCLAP::SwitchArg a_file("", "file", "Compress files too, not just directories.", cmd);
    TCLAP::SwitchArg a_skip_empty("", "skip_empty", "Skip zipping empty directories.", cmd);
    TCLAP::SwitchArg a_skip_exists("", "skip_existing", "Dont't zip when the output file exists.", cmd);
    TCLAP::SwitchArg a_all("a", "all", "Do not ignore hidden files (i.e. entries starting with \".\").", cmd);
    TCLAP::SwitchArg a_dryrun("", "dryrun", "List subdirectories and exit.", cmd);
    cmd.parse(argc, argv);

    auto input_dir = fs::path(a_input.getValue());
    auto output_dir = fs::path(a_output.isSet() ? a_output.getValue() : a_input.getValue());
    auto depth = a_depth.getValue();
    auto jobs = a_jobs.getValue();
    auto level = a_level.getValue();
    auto subdirs = list_subdirs(input_dir, depth, a_all.isSet(), a_file.isSet());

    if (a_file.isSet()) {
      auto result = std::remove_if(subdirs.begin(), subdirs.end(), [](auto& d) {
        return d.extension() == ".zip";
        });
      subdirs.erase(result, subdirs.end());
    }
    if (a_skip_exists.isSet()) {
      auto result = std::remove_if(subdirs.begin(), subdirs.end(), [&input_dir, &output_dir](auto& d) {
        auto output = input2output(input_dir, output_dir, d);
        return fs::exists(output);
        });
      auto orig_size = subdirs.size();
      subdirs.erase(result, subdirs.end());
      cout << "Skip " << orig_size - subdirs.size() << " existing entries." << endl;
    }
    if (a_skip_empty.isSet()) {
      auto orig_size = subdirs.size();
      auto result = std::remove_if(subdirs.begin(), subdirs.end(), [](auto& d) {
        return fs::is_directory(d) && fs::is_empty(d);
        });
      subdirs.erase(result, subdirs.end());
      cout << "Skip " << orig_size - subdirs.size() << " empty directories." << endl;
    }
    if (subdirs.empty()) {
      cout << "There is nothing to compress." << endl;
      return 0;
    }
    if (a_dryrun.isSet()) {
      auto mode = local_setmode();
      for (const auto& d : subdirs) {
        auto output = input2output(input_dir, output_dir, d);
        WCOUT << d.WSTRING() << " -> " << output.WSTRING() << '\n';
      }
      cout << flush;
      return 0;
    }
    using namespace indicators;
    ProgressBar bar{
       option::BarWidth{30},
       option::MaxProgress(subdirs.size()),
       option::Start{"["},
       option::Fill{"="},
       option::Lead{">"},
       option::Remainder{" "},
       option::End{"]"},
       option::PrefixText{"Compressing"},
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
      threads.emplace_back([job_id, jobs, level, &subdirs, &input_dir, &output_dir, &bar, &mtx_mkdir, &ep]() {
        for (int i = job_id; i < subdirs.size(); i += jobs) {
          const auto& subdir = subdirs[i];
          auto output = input2output(input_dir, output_dir, subdir);
          {
            std::lock_guard<std::mutex> lock(mtx_mkdir);
            if (!fs::exists(output.parent_path())) {
              fs::create_directories(output.parent_path());
            }
          }
          try {
            zip_directory(subdir, output, level);
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
  catch (std::system_error& e) {
    cerr << e.code() << endl;
    cerr << e.what() << endl;
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
