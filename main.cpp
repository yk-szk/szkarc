#include <filesystem>
#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>
#include <mz.h>
#include <mz_strm.h>
#include <mz_strm_os.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>
#include <tclap/CmdLine.h>
#include <config.h>

using namespace std;
namespace fs = std::filesystem;


class ZipReader
{
public:
  void* zip_reader;
  void* file_stream;
  int32_t err;
  ZipReader(const char* path)
    : zip_reader(NULL), file_stream(NULL)
  {
    mz_zip_reader_create(&zip_reader);
    mz_stream_os_create(&file_stream);

    err = mz_stream_os_open(file_stream, path, MZ_OPEN_MODE_READ);
    if (err == MZ_OK) {
      err = mz_zip_reader_open(zip_reader, file_stream);
    }
  }

  ~ZipReader()
  {
    mz_zip_reader_close(zip_reader);
    mz_stream_os_delete(&file_stream);
    mz_zip_reader_delete(&zip_reader);
  }
};

class ZipWriter
{
public:
  void* zip_writer;
  void* file_stream;
  int32_t err;
  ZipWriter(const char* path)
    : zip_writer(NULL), file_stream(NULL)
  {
    mz_zip_writer_create(&zip_writer);
    mz_stream_os_create(&file_stream);

    err = mz_stream_os_open(file_stream, path, MZ_OPEN_MODE_READ);
    if (err == MZ_OK) {
      //   err = mz_zip_writer_open(zip_writer, file_stream);
    }
  }

  ~ZipWriter()
  {
    mz_zip_writer_close(zip_writer);
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

      size_t total_size = std::transform_reduce(subdirs.cbegin(), subdirs.cend(), 0u, plus{}, [](const PathList &l) {return l.size(); });
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
    TCLAP::CmdLine cmd("Zip each subdirectory", ' ', PROJECT_VERSION);

    TCLAP::UnlabeledValueArg<std::string> a_input("input", "Input directory", true, "", "input");
    cmd.add(a_input);
    TCLAP::UnlabeledValueArg<std::string> a_output("output", "(optional) Output directory.", false, "", "output");
    cmd.add(a_output);
    TCLAP::ValueArg<std::string> a_depth("d", "depth", "(optional) Depth of the subdirectories.", false, "0", "int");
    cmd.add(a_depth);

    TCLAP::SwitchArg a_dryrun("", "dryrun", "List subdirectories and exit.", cmd, false);
    cmd.parse(argc, argv);

    auto input_dir = fs::path(a_input.getValue());
    auto output_dir = fs::path(a_output.isSet() ? a_output.getValue() : a_input.getValue());
    auto depth = stoi(a_depth.getValue());
    auto dirs = list_subdirs(input_dir, depth);
    if (a_dryrun.isSet()) {
      for (const auto &d : dirs) {
        cout << d.string() << '\n';
      }
      cout << flush;
      return 0;
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
