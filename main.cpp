#include <filesystem>
#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>
#include <thread>
#include <mz.h>
#include <mz_strm.h>
#include <mz_strm_os.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>
#include <tclap/CmdLine.h>
#include <indicators/progress_bar.hpp>
#include <config.h>

//using namespace std;
namespace fs = std::filesystem;
using std::cout;
using std::cerr;
using std::endl;
using std::flush;
using std::runtime_error;

int get_core_counts() {
#if defined(WIN32)
  return std::thread::hardware_concurrency() / 2;
#else
  return std::thread::hardware_concurrency() / 2;
#endif
}

class ZipWriter
{
private:
  int16_t level;
public:
  void* zip_writer;
  void* file_stream;
  int32_t err;
  ZipWriter(const char* path, int16_t level_)
    : zip_writer(NULL), file_stream(NULL), level(level_)
  {
    mz_zip_writer_create(&zip_writer);
    mz_stream_os_create(&file_stream);
    mz_zip_writer_set_compress_method(zip_writer, MZ_COMPRESS_METHOD_DEFLATE);
    mz_zip_writer_set_compress_level(zip_writer, level);
    err = mz_stream_os_open(file_stream, path, MZ_OPEN_MODE_WRITE | MZ_OPEN_MODE_CREATE);
    if (err != MZ_OK) {
      throw runtime_error("Failed to open a zip file.");
    }
    err = mz_zip_writer_open(zip_writer, file_stream, 0);
    if (err != MZ_OK) {
      throw runtime_error("Failed to open a zip file.");
    }
  }

  void add_dir(const fs::path& path) {
    err = mz_zip_writer_add_path(zip_writer, path.string().c_str(), NULL, 0, 1);
  }

  ~ZipWriter()
  {
    err = mz_zip_writer_close(zip_writer);
    if (err != MZ_OK) {
      throw runtime_error("Failed to close the zip writer");
    }
    mz_stream_os_delete(&file_stream);
    mz_zip_writer_delete(&zip_writer);
  }
};

using PathList = std::vector<fs::path>;
PathList list_subdirs(const fs::path &indir, int depth) {
  PathList list;
  for (const auto &ent : fs::directory_iterator(indir)) {
    if (ent.is_directory()) {
      list.push_back(ent.path());
    }
  }
  std::sort(list.begin(), list.end());
  if (depth == 0) {
    return list;
  }
  else {
    std::vector<PathList> subdirs;
    std::transform(list.cbegin(), list.cend(), std::back_inserter(subdirs), [depth](const fs::path &p) {
      return list_subdirs(p, depth - 1);
      });

    size_t total_size = std::transform_reduce(subdirs.cbegin(), subdirs.cend(), 0u, std::plus{}, [](const PathList &l) {return l.size(); });
    PathList flat;
    flat.reserve(total_size);
    for (auto &subd : subdirs) {
      flat.insert(
        flat.end(),
        std::make_move_iterator(subd.begin()),
        std::make_move_iterator(subd.end())
      );
    }
    return flat;
  }
}

int main(int argc, char* argv[])
{

  try {
    TCLAP::CmdLine cmd("Zip each subdirectory. version: " PROJECT_VERSION, ' ', PROJECT_VERSION);

    TCLAP::UnlabeledValueArg<std::string> a_input("input", "Input directory", true, "", "input");
    cmd.add(a_input);
    TCLAP::UnlabeledValueArg<std::string> a_output("output", "(optional) Output directory.", false, "", "output");
    cmd.add(a_output);
    TCLAP::ValueArg<std::string> a_depth("d", "depth", "(optional) Depth of the subdirectories.", false, "0", "int");
    cmd.add(a_depth);
    TCLAP::ValueArg<std::string> a_jobs("j", "jobs", "(optional) Number of simultaneous jobs.", false, "0", "int");
    cmd.add(a_jobs);
    TCLAP::ValueArg<std::string> a_level("l", "level", "(optional) Compression level.", false, "1", "int");
    cmd.add(a_level);

    TCLAP::SwitchArg a_dryrun("", "dryrun", "List subdirectories and exit.", cmd, false);
    cmd.parse(argc, argv);

    auto input_dir = fs::path(a_input.getValue());
    auto output_dir = fs::path(a_output.isSet() ? a_output.getValue() : a_input.getValue());
    auto depth = stoi(a_depth.getValue());
    auto jobs = stoi(a_jobs.getValue());
    auto level = stoi(a_level.getValue());
    auto subdirs = list_subdirs(input_dir, depth);
    if (a_dryrun.isSet()) {
      for (const auto &d : subdirs) {
        auto relative = d.lexically_relative(input_dir);
        auto output = (output_dir / relative).string() + ".zip";
        cout << d.string() << " -> " << output << '\n';
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
      jobs = get_core_counts();
      cout << "Using " << jobs << " cores." << endl;
    }
    std::mutex mtx_mkdir;
    std::vector<std::thread> threads;
    threads.reserve(jobs);
    for (int job_id = 0; job_id < jobs; ++job_id) {
      threads.emplace_back([job_id, jobs, level, &subdirs, &input_dir, &output_dir, &bar, &mtx_mkdir]() {
        for (int i = job_id; i < subdirs.size(); i += jobs) {
          auto &subdir = subdirs[i];
          auto relative = subdir.lexically_relative(input_dir);
          auto output = output_dir / (relative.string() + ".zip");
          mtx_mkdir.lock();
          if (!fs::exists(output.parent_path())) {
            fs::create_directories(output.parent_path());
          }
          mtx_mkdir.unlock();
          ZipWriter writer(output.string().c_str(), level);
          writer.add_dir(subdir);
          bar.tick();
        }
        });
    }
    for (auto &t : threads) {
      t.join();
    }
  }
  catch (TCLAP::ArgException& e)
  {
    std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
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
