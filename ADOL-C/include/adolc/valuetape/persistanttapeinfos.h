
#ifndef ADOLC_PERSISTANT_TAPE_INFOS_H
#define ADOLC_PERSISTANT_TAPE_INFOS_H

#include <array>
#include <cstring>
#include <string>

// should remain in a buffer, if we re-use a tape
struct PersistantTapeInfos {
  // storage order used by the tape I/O layer
  enum FILES { OPERATIONS_FILE, LOCATIONS_FILE, VALUES_FILE, TAYLORS_FILE };
  std::array<char *, 4> fileNames{};
  // tape types => used for file name generation
  enum TAPENAMES { LOCATIONS_TAPE, VALUES_TAPE, OPERATIONS_TAPE, TAYLORS_TAPE };

  ~PersistantTapeInfos();
  PersistantTapeInfos() = default;
  PersistantTapeInfos(short tapeId) {
    fileNames[OPERATIONS_FILE] = createFileName(tapeId, OPERATIONS_TAPE);
    fileNames[LOCATIONS_FILE] = createFileName(tapeId, LOCATIONS_TAPE);
    fileNames[VALUES_FILE] = createFileName(tapeId, VALUES_TAPE);
    fileNames[TAYLORS_FILE] = createFileName(tapeId, TAYLORS_TAPE);
  }
  PersistantTapeInfos(short tapeId, std::array<std::string, 4> &&tapeBaseNames)
      : tapeBaseNames_(std::move(tapeBaseNames)) {
    fileNames[OPERATIONS_FILE] = createFileName(tapeId, OPERATIONS_TAPE);
    fileNames[LOCATIONS_FILE] = createFileName(tapeId, LOCATIONS_TAPE);
    fileNames[VALUES_FILE] = createFileName(tapeId, VALUES_TAPE);
    fileNames[TAYLORS_FILE] = createFileName(tapeId, TAYLORS_TAPE);
  }
  PersistantTapeInfos(const PersistantTapeInfos &) = delete;
  PersistantTapeInfos(PersistantTapeInfos &&other) noexcept;
  PersistantTapeInfos &operator=(const PersistantTapeInfos &) = delete;
  PersistantTapeInfos &operator=(PersistantTapeInfos &&other) noexcept;

  // create file name depending on tape type and number
  char *createFileName(short tapeId, int tapeType);
  /****************************************************************************/
  /* Tries to read a local config file containing, e.g., buffer sizes         */
  /****************************************************************************/
  static char *duplicatestr(const char *instr) {
    size_t len = std::strlen(instr);
    char *outstr = new char[len + 1];
    std::strncpy(outstr, instr, len);
    return outstr;
  }

  int forodec_nax{0};
  int forodec_dax{0};
  double *forodec_y{nullptr};
  double *forodec_z{nullptr};
  double **forodec_Z{nullptr};
  double **jacSolv_J{nullptr};
  double **jacSolv_I{nullptr};
  double *jacSolv_xold{nullptr};
  int *jacSolv_ri{nullptr};
  int *jacSolv_ci{nullptr};
  int jacSolv_nax{0};
  int jacSolv_modeold{0};
  int jacSolv_cgd{0};

  // the base names of every tape type
  std::array<std::string, 4> tapeBaseNames_;

  //  - remember if tapes shall be written out to disk
  // - this information can only be given at taping time and must survive all
  // other actions on the tape
  int keepTape{0};

  // defaults to 0, if 1 skips file removal (when file operations are costly)
  int skipFileCleanup{0};

  double *paramstore{nullptr};
};

#endif // ADOLC_PERSISTANT_TAPE_INFOS_H
