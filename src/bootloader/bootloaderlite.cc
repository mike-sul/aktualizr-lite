#include "bootloaderlite.h"

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>

#include "bootloader/bootloader.h"
#include "storage/invstorage.h"
#include "utilities/exceptions.h"

namespace bootloader {

BootloaderLite::BootloaderLite(BootloaderConfig config, INvStorage& storage, OSTree::Sysroot::Ptr sysroot)
    : Bootloader(std::move(config), storage), sysroot_{std::move(sysroot)} {}

void BootloaderLite::installNotify(const Uptane::Target& target) const {
  std::string sink;
  switch (config_.rollback_mode) {
    case RollbackMode::kBootloaderNone:
    case RollbackMode::kUbootGeneric:
      break;
    case RollbackMode::kUbootMasked:
    case RollbackMode::kFioVB: {
      std::string target_version = getVersion(sysroot_->deployment_path(), target.sha256Hash());
      if (target_version.empty()) {
        return;
      }
      const auto cur_version{getEnvVar("bootfirmware_version")};
      if (std::get<1>(cur_version)) {
        LOG_INFO << "Current bootloader version: " << std::get<0>(cur_version);
      } else {
        LOG_WARNING << "Failed to get bootloader version: " << std::get<0>(cur_version);
      }
      if (std::get<0>(cur_version) != target_version) {
        LOG_INFO << "Bootloader will be updated to version: " << target_version;
        setEnvVar("bootupgrade_available", "1");
      }
    } break;
    default:
      throw NotImplementedException();
  }
}

std::string BootloaderLite::getVersion(const std::string& deployment_dir, const std::string& hash,
                                       const std::string& ver_file) {
  try {
    std::string file;
    for (auto& p : boost::filesystem::directory_iterator(deployment_dir)) {
      std::string dir = p.path().string();
      if (!boost::filesystem::is_directory(dir)) {
        continue;
      }
      if (boost::algorithm::contains(dir, hash)) {
        file = dir + ver_file;
        break;
      }
    }
    if (file.empty()) {
      LOG_WARNING << "Target hash not found";
      return std::string();
    }

    LOG_INFO << "Target firmware file: " << file;
    std::ifstream ifs(file);
    std::string version((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    std::string watermark = "bootfirmware_version";
    std::string::size_type i = version.find(watermark);
    if (i != std::string::npos) {
      version.erase(i, watermark.length() + 1);
      LOG_INFO << "Target firmware version: " << version;
      return version;
    } else {
      LOG_WARNING << "Target firmware version not found";
      return std::string();
    }
  } catch (const std::exception& exc) {
    LOG_ERROR << "Failed to obtain Target firmware version:  " << exc.what();
    return "";
  }
}

bool BootloaderLite::isUpdateInProgress() const {
  const auto ba_val{getEnvVar("bootupgrade_available")};
  if (!std::get<1>(ba_val)) {
    LOG_ERROR << "Failed to get `bootupgrade_available` value, assuming it is set to 0; err: " << std::get<0>(ba_val);
    return false;
  }
  return std::get<0>(ba_val) == "1";
}

bool BootloaderLite::setEnvVar(const std::string& var_name, const std::string& var_val) const {
  const std::unordered_map<RollbackMode, std::string> typeToCmd{
      {RollbackMode::kUbootMasked, "fw_setenv"},
      {RollbackMode::kFioVB, "fiovb_setenv"},
  };
  if (0 == typeToCmd.count(config_.rollback_mode)) {
    LOG_DEBUG << "No command to set environment variable found for the given bootloader type: "
              << config_.rollback_mode;
    return false;
  }
  const auto cmd{boost::format{"%s %s %s"} % typeToCmd.at(config_.rollback_mode) % var_name % var_val};
  std::string output;
  if (Utils::shell(cmd.str(), &output) != 0) {
    LOG_WARNING << "Failed to set the bootloader's environment variable" << var_name << "; err: " << output;
    return false;
  }
  return true;
}

std::tuple<std::string, bool> BootloaderLite::getEnvVar(const std::string& var_name) const {
  const std::unordered_map<RollbackMode, std::string> typeToCmd{
      {RollbackMode::kUbootMasked, "fw_printenv -n"},
      {RollbackMode::kFioVB, "fiovb_printenv"},
  };
  if (0 == typeToCmd.count(config_.rollback_mode)) {
    const auto er_msg{boost::format("No command to read environment variable found for `%s` bootloader type") %
                      config_.rollback_mode};
    return {er_msg.str(), false};
  }
  const auto cmd{boost::format{"%s %s"} % typeToCmd.at(config_.rollback_mode) % var_name};
  std::string output;
  if (Utils::shell(cmd.str(), &output) != 0) {
    const auto er_msg{boost::format("Failed to get a bootloader environment variable; cmd: %s, err: %s") % cmd %
                      output};
    return {er_msg.str(), false};
  }
  return {output, true};
}

}  // namespace bootloader
