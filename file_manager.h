#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include "board_config.h"
#include <Arduino.h>

namespace FileManager {
  void fileManagerSetup();
  void fileManagerLoop();
  void fileManagerCleanup();
}

#endif // FILE_MANAGER_H
