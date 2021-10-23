#include <filesystem>
#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>
#include <thread>
#include <mz.h>
#include <mz_os.h>
#include <mz_strm.h>
#include <mz_strm_os.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>
#include <windows.h>
#include <tclap/CmdLine.h>
#include <indicators/progress_bar.hpp>
#include <config.h>

namespace fs = std::filesystem;
using std::cout;
using std::cerr;
using std::endl;
using std::flush;
using std::runtime_error;

int get_physical_core_counts() {
#if defined(WIN32)
  typedef BOOL(WINAPI* LPFN_GLPI)(
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION,
    PDWORD);
  LPFN_GLPI glpi;
  BOOL done = FALSE;
  PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
  PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr = NULL;
  DWORD returnLength = 0;
  DWORD byteOffset = 0;
  PCACHE_DESCRIPTOR Cache;
  int processorCoreCount = 0;

  glpi = (LPFN_GLPI)GetProcAddress(GetModuleHandle(TEXT("kernel32")), "GetLogicalProcessorInformation");
  if (NULL == glpi) {
    std::runtime_error("GetLogicalProcessorInformation is not supported.");
  }
  while (!done) {
    DWORD rc = glpi(buffer, &returnLength);
    if (FALSE == rc) {
      if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        if (buffer)
          free(buffer);
        buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(
          returnLength);

        if (NULL == buffer) {
          std::runtime_error("Error: Allocation failure");
        }
      }
      else {
        std::runtime_error("Error");
      }
    }
    else {
      done = TRUE;
    }
  }
  ptr = buffer;
  while (byteOffset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= returnLength) {
    if (ptr->Relationship == RelationProcessorCore) {
      processorCoreCount++;
    }
    byteOffset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    ptr++;
  }
  return processorCoreCount;
#else
  return std::thread::hardware_concurrency();
#endif
}

int32_t stream_os_open(void* stream, const char* path, int32_t mode) {
  typedef struct mz_stream_win32_s {
    mz_stream       stream;
    HANDLE          handle;
    int32_t         error;
  } mz_stream_win32;

  mz_stream_win32* win32 = (mz_stream_win32*)stream;
  uint32_t desired_access = 0;
  uint32_t creation_disposition = 0;
  uint32_t share_mode = FILE_SHARE_READ;
  uint32_t flags_attribs = FILE_ATTRIBUTE_NORMAL;
  wchar_t* path_wide = NULL;


  if (path == NULL)
    return MZ_PARAM_ERROR;

  /* Some use cases require write sharing as well */
  share_mode |= FILE_SHARE_WRITE;

  if ((mode & MZ_OPEN_MODE_READWRITE) == MZ_OPEN_MODE_READ) {
    desired_access = GENERIC_READ;
    creation_disposition = OPEN_EXISTING;
  }
  else if (mode & MZ_OPEN_MODE_APPEND) {
    desired_access = GENERIC_WRITE | GENERIC_READ;
    creation_disposition = OPEN_EXISTING;
  }
  else if (mode & MZ_OPEN_MODE_CREATE) {
    desired_access = GENERIC_WRITE | GENERIC_READ;
    creation_disposition = CREATE_ALWAYS;
  }
  else {
    return MZ_PARAM_ERROR;
  }

  path_wide = mz_os_unicode_string_create(path, MZ_ENCODING_CODEPAGE_932);
  if (path_wide == NULL)
    return MZ_PARAM_ERROR;

  win32->handle = CreateFileW(path_wide, desired_access, share_mode, NULL,
    creation_disposition, flags_attribs, NULL);

  mz_os_unicode_string_delete(&path_wide);

  if (mz_stream_os_is_open(stream) != MZ_OK) {
    win32->error = GetLastError();
    return MZ_OPEN_ERROR;
  }

  if (mode & MZ_OPEN_MODE_APPEND)
    return mz_stream_os_seek(stream, 0, MZ_SEEK_END);

  return MZ_OK;
}

std::string wstr2utf8(std::wstring const& src)
{
  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
  return converter.to_bytes(src);
}

void  zip_directory(const fs::path& input, const fs::path& output, int16_t level) {
  void* zip_writer;
  void* file_stream;
  int32_t err;
  mz_zip_writer_create(&zip_writer);
  mz_stream_os_create(&file_stream);
  mz_zip_writer_set_compress_method(zip_writer, MZ_COMPRESS_METHOD_DEFLATE);
  mz_zip_writer_set_compress_level(zip_writer, level);
#ifdef WIN32
  err = stream_os_open(file_stream, output.string().c_str(), MZ_OPEN_MODE_WRITE | MZ_OPEN_MODE_CREATE);
#else
  err = mz_stream_os_open(file_stream, path, MZ_OPEN_MODE_WRITE | MZ_OPEN_MODE_CREATE);
#endif
  if (err != MZ_OK) {
    throw runtime_error("Failed to open a zip file.");
  }
  err = mz_zip_writer_open(zip_writer, file_stream, 0);
  if (err != MZ_OK) {
    throw runtime_error("Failed to open a zip file.");
  }

#ifdef WIN32
  auto utf8 = wstr2utf8(input.wstring());
  err = mz_zip_writer_add_path(zip_writer, utf8.c_str(), NULL, 0, 1);
#else
  err = mz_zip_writer_add_path(zip_writer, input.string().c_str(), NULL, 0, 1);
#endif
  if (err != MZ_OK) {
    throw runtime_error("Failed to compress");
  }

  err = mz_zip_writer_close(zip_writer);
  if (err != MZ_OK) {
    throw runtime_error("Failed to close the zip writer");
  }
  mz_stream_os_delete(&file_stream);
  mz_zip_writer_delete(&zip_writer);
};

using PathList = std::vector<fs::path>;
PathList list_subdirs(const fs::path& indir, int depth, bool include_files) {
  PathList list;
  for (const auto& ent : fs::directory_iterator(indir)) {
    if (ent.is_directory()) {
      list.push_back(ent.path());
    }
    else if (include_files) {
      list.push_back(ent.path());
    }
  }
  std::sort(list.begin(), list.end());
  if (depth == 0) {
    return list;
  }
  else {
    std::vector<PathList> subdirs;
    std::transform(list.cbegin(), list.cend(), std::back_inserter(subdirs), [depth, include_files](const fs::path& p) {
      return list_subdirs(p, depth - 1, include_files);
      });

    size_t total_size = std::transform_reduce(subdirs.cbegin(), subdirs.cend(), 0u, std::plus{}, [](const PathList& l) {return l.size(); });
    PathList flat;
    flat.reserve(total_size);
    for (auto& subd : subdirs) {
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

    TCLAP::SwitchArg a_file("", "file", "Compress files too, not just directories.", cmd, false);
    TCLAP::SwitchArg a_dryrun("", "dryrun", "List subdirectories and exit.", cmd, false);
    cmd.parse(argc, argv);

    auto input_dir = fs::path(a_input.getValue());
    auto output_dir = fs::path(a_output.isSet() ? a_output.getValue() : a_input.getValue());
    auto depth = stoi(a_depth.getValue());
    auto jobs = stoi(a_jobs.getValue());
    auto level = stoi(a_level.getValue());
    auto subdirs = list_subdirs(input_dir, depth, a_file.isSet());
    if (a_dryrun.isSet()) {
      for (const auto& d : subdirs) {
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
      jobs = get_physical_core_counts();
      cout << "Using " << jobs << " cores." << endl;
    }
    std::mutex mtx_mkdir;
    std::vector<std::thread> threads;
    threads.reserve(jobs);
    for (int job_id = 0; job_id < jobs; ++job_id) {
      threads.emplace_back([job_id, jobs, level, &subdirs, &input_dir, &output_dir, &bar, &mtx_mkdir]() {
        for (int i = job_id; i < subdirs.size(); i += jobs) {
          auto& subdir = subdirs[i];
          auto relative = subdir.lexically_relative(input_dir);
          auto output = output_dir / (relative.string() + ".zip");
          mtx_mkdir.lock();
          if (!fs::exists(output.parent_path())) {
            fs::create_directories(output.parent_path());
          }
          mtx_mkdir.unlock();
          zip_directory(subdir, output, level);
          bar.tick();
        }
        });
    }
    for (auto& t : threads) {
      t.join();
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
