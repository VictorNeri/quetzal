#ifndef HOST_SCANNER_H
#define HOST_SCANNER_H

#include "board_config.h"
#include <Arduino.h>

namespace HostScanner {
  void hostScannerSetup();
  void hostScannerLoop();
}

#endif  // HOST_SCANNER_H
