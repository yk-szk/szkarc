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

int main(int argc, char* argv[])
{
  try {
    TCLAP::CmdLine cmd("Delete directory tree(s) matching specified conditions.", ' ', PROJECT_VERSION);

    TCLAP::UnlabeledValueArg<std::string> a_input("input", "Input directory", true, "", "input", cmd);
    TCLAP::ValueArg<int> a_depth("d", "depth", "Depth of the subdirectories.", false, 0, "int", cmd);
    TCLAP::SwitchArg a_yes("y", "yes", "Delete directories without asking.", cmd);
    TCLAP::SwitchArg a_exec("e", "exec", "Execute deletion.", cmd);

    TCLAP::MultiArg<std::string> a_present("p", "present", "Present condition. Directories containing specified filename/dirname get deleted.", false, "filename", cmd);
    TCLAP::MultiArg<std::string> a_absent("a", "absent", "Absent condition. Directories not containing specified filename/dirname get deleted.", false, "filename", cmd);
    cmd.parse(argc, argv);

    auto input_dir = fs::path(a_input.getValue());
    auto subdirs = list_subdirs(input_dir, a_depth.getValue(), false);
    const std::vector<std::string>& cond_present = a_present.getValue();
    const std::vector<std::string>& cond_absent = a_absent.getValue();

    if (cond_present.empty() && cond_absent.empty()) {
      cerr << "At least one condition needs to be set." << endl;
      return 1;
    }

    // filter directories based on the conditions
    auto remove_result = std::remove_if(subdirs.begin(), subdirs.end(), [&cond_absent, &cond_present](const auto& subdir) {
      bool absent_unmet = std::any_of(cond_absent.cbegin(), cond_absent.cend(),
        [&subdir](const std::string& filename) {return fs::exists(subdir / fs::path(filename)); });
      if (absent_unmet) {
        return true;
      }
      bool present_unmet = std::any_of(cond_absent.cbegin(), cond_absent.cend(),
        [&subdir](const std::string& filename) {return !fs::exists(subdir / fs::path(filename)); });
      if (present_unmet) {
        return true;
      }
      return false;
      });
    subdirs.erase(remove_result, subdirs.end());
    if (subdirs.empty()) {
      cout << "There is nothing to delete." << endl;
      return 0;
    }
    if (a_exec.isSet()) {
      cout << "Deleting directories..." << endl;
      for (const auto& subdir : subdirs) {
        auto msg = "Delete \"" + subdir.string() + "\"? (Y/N): ";
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
          cout << subdir << flush;
          fs::remove_all(subdir);
        }
      }
    }
    else {  // dryrun
      cout << "Dryrun. Add \"--exec\" option to execute the deletion.\n";
      for (const auto& subdir : subdirs) {
        cout << subdir.string() << '\n';
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
