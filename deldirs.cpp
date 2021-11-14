#include <filesystem>
#include <iostream>
#include <vector>
#include <numeric>
#include <exception>
#include <tclap/CmdLine.h>
#include <config.h>
#include "szkarc.h"

namespace fs = std::filesystem;
using std::cout;
using std::cerr;
using std::endl;
using std::flush;

struct Pattern
{
  size_t depth;
  std::string filename;
  Pattern(size_t d, const std::string& fn)
    : depth(d), filename(fn)
  {
  }
};
#define EXAMPLES "\
===Example patterns===\n\
Delete directories that contain <filename>\n\
 --present filename\n\
Delete directories that does not contain <filename>.\n\
 --absent filename"

int main(int argc, char* argv[])
{
  try {
    TCLAP::CmdLine cmd("Delete directory tree(s) matching specified conditions.\n" EXAMPLES, ' ', PROJECT_VERSION);

    TCLAP::UnlabeledValueArg<std::string> a_input("input", "Input directory", true, "", "input", cmd);
    TCLAP::ValueArg<int> a_depth("d", "depth", "(optional) Depth of the subdirectories.", false, 0, "int", cmd);
    TCLAP::SwitchArg a_yes("y", "yes", "Delete directories without asking.", cmd);
    TCLAP::SwitchArg a_exec("e", "exec", "Execute deletion.", cmd);
    TCLAP::MultiSwitchArg a_verbose("v", "verbose", "Verbose switch.", cmd);

    TCLAP::UnlabeledMultiArg<std::string> a_pattern("pattern", "Conditions for deletion", true, "pattern", cmd);
    cmd.parse(argc, argv);

    auto input_dir = fs::path(a_input.getValue());
    auto verbosity = a_verbose.getValue();
    auto subdirs = list_subdirs(input_dir, a_depth.getValue(), false);
    // parse pattern arguments
    std::vector<Pattern> pat_present, pat_absent;
    {
      auto patterns = a_pattern.getValue();
      if (patterns.size() < 2) {
        cerr << "Invalid pattern argument." << endl;
        return 1;
      }
      size_t i = 0;
      while (i < patterns.size()) {
        std::string& opt = patterns[i];
        if (opt == "-p" || opt == "--present" || opt == "-a" || opt == "--absent") {
          size_t j;
          for (j = i + 1; j < patterns.size(); ++j) {
            if (patterns[j][0] == '-') {
              break;
            }
          }
          switch (j - i) {
          case 1:
            cerr << "Name is required after " << opt << endl;
            return 1;
          case 2:  // name without depth
            if (opt[1] == 'p' || opt[2] == 'p') {
              pat_present.emplace_back(Pattern(0, patterns[i + 1]));
            }
            else {
              pat_absent.emplace_back(Pattern(0, patterns[i + 1]));
            }
            break;
          case 3: // name with depth
          {
            std::string& depth_str = patterns[i + 2];
            if (!std::all_of(depth_str.begin(), depth_str.end(), ::isdigit))
            {
              cerr << depth_str << " is not a number." << endl;
              return 1;
            }
            int depth = std::stoi(depth_str);
            if (depth != 0) {
              cerr << depth << " is an invalid value." << endl;
              cerr << "Only 0 is supported (now)." << endl;
              return 1;
            }
            if (opt[1] == 'p' || opt[2] == 'p')
            {
              pat_present.emplace_back(Pattern(depth, patterns[i + 1]));
            }
            else
            {
              pat_absent.emplace_back(Pattern(depth, patterns[i + 1]));
            }
            break;
          }
          default:
            cerr << "Too many arguments for " << opt << endl;
            return 1;
          }
          i = j;
        }
        else {
          cerr << "Invalid pattern argument: \"" << opt << '"' << endl;
          return 1;
        }
      }
    }
    if (verbosity > 0) {
      if (!pat_present.empty()) {
        cout << "Present patterns" << '\n';
        for (const auto& pat : pat_present) {
          cout << pat.filename << ' ' << pat.depth << '\n';
        }
      }
      if (!pat_absent.empty()) {
        cout << "Absent patterns" << '\n';
        for (const auto& pat : pat_absent) {
          cout << pat.filename << ' ' << pat.depth << '\n';
        }
      }
      cout << flush;
    }

    // filter directories based on the conditions
    std::vector<size_t> delete_indices;
    for (size_t i = 0; i < subdirs.size(); ++i) {
      const auto& subdir = subdirs[i];
      std::vector<std::string> filenames;
      for (const auto& ent : fs::directory_iterator(subdir)) {
        filenames.push_back(ent.path().filename().string());
      }
      // check absent conditions
      for (const auto& filename : filenames)
      {
        for (const auto& pat : pat_absent)
        {
          if (pat.filename == filename)
          {
            goto NEXT_SUBDIR;
          }
        }
      }
      // check present conditions
      for (const auto& pat : pat_present)
      {
        for (const auto& filename : filenames)
        {
          if (pat.filename == filename)
          {
            goto NEXT_PAT;
          }
        }
        goto NEXT_SUBDIR;  // pat.filename does not exist
      NEXT_PAT:;
      }
      delete_indices.push_back(i);
    NEXT_SUBDIR:;
    }
    if (delete_indices.empty()) {
      cout << "There is nothing to delete." << endl;
      return 0;
    }
    if (a_exec.isSet()) {
      cout << "Execute deletion" << endl;
      for (size_t delete_index : delete_indices) {
        auto subdir = subdirs[delete_index].string();
        auto msg = "Delete \"" + subdir + "\"? (Y/N): ";
        bool yes = a_yes.isSet();  // if --yes option is set, following while loop is skipped.
        while (!yes) {
          cout << msg << flush;
          std::string ans;
          std::cin >> ans;
          if (ans == "y" || ans == "Y") {
            yes = true;
            break;
          }
          if (ans == "n" || ans == "N") {
            yes = false;
            break;
          }
          cout << "Answer by Y/N." << endl;
        }
        if (yes) {
          fs::remove_all(subdir);
        }
      }
    }
    else {  // dryrun
      for (size_t delete_index : delete_indices) {
        cout << subdirs[delete_index].string() << '\n';
      }
      cout << flush;
      return 0;
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
