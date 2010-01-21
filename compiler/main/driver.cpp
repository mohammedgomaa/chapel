#define EXTERN
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include "chpl.h"
#include "arg.h"
#include "countTokens.h"
#include "driver.h"
#include "files.h"
#include "misc.h"
#include "mysystem.h"
#include "runpasses.h"
#include "stmt.h"
#include "stringutil.h"
#include "version.h"
#include "log.h"
#include "primitive.h"


FILE* html_index_file = NULL;


char CHPL_HOME[FILENAME_MAX] = "";

const char* CHPL_HOST_PLATFORM = NULL;
const char* CHPL_TARGET_PLATFORM = NULL;
const char* CHPL_HOST_COMPILER = NULL;
const char* CHPL_TARGET_COMPILER = NULL;
const char* CHPL_TASKS = NULL;
const char* CHPL_COMM = NULL;

int fdump_html = 0;
static char libraryFilename[FILENAME_MAX] = "";
static char incFilename[FILENAME_MAX] = "";
static char moduleSearchPath[FILENAME_MAX] = "";
static char log_flags[512] = "";
static bool rungdb = false;
bool fRuntime = false;
bool no_codegen = false;
int debugParserLevel = 0;
bool developer = false;
bool ignore_errors = false;
bool ignore_warnings = false;
int trace_level = 0;
int fcg = 0;
static bool fBaseline = false;
static bool fFastFlag = false;
int fConditionalDynamicDispatchLimit = 0;
bool fNoCopyPropagation = false;
bool fNoDeadCodeElimination = false;
bool fNoScalarReplacement = false;
bool fNoRemoteValueForwarding = false;
bool fNoRemoveCopyCalls = false;
bool fNoOptimizeLoopIterators = false;
bool fNoInlineIterators = false;
bool fNoLiveAnalysis = false;
bool fNoBoundsChecks = false;
bool fNoLocalChecks = false;
bool fNoNilChecks = false;
bool fNoChecks = false;
bool fNoInline = false;
bool fNoPrivatization = false;
bool fNoOptimizeOnClauses = false;
int optimize_on_clause_limit = 20;
bool fGenIDS = false;
bool fSerialForall = false;
bool fSerial;  // initialized in setupOrderedGlobals() below
bool fLocal;   // initialized in setupOrderedGlobals() below
bool genCommunicatedStructures; // initialized in setupOrderedGlobals() below
bool fGPU;
bool fieeefloat = true;
bool report_inlining = false;
char chplmake[256] = "";
char fExplainCall[256] = "";
char fExplainInstantiation[256] = "";
bool fCLineNumbers = false;
char fPrintStatistics[256] = "";
bool fPrintDispatch = false;
bool fWarnPromotion = false;
bool printCppLineno = false;
bool userSetCppLineno = false;
int num_constants_per_variable = 1;
char defaultDist[256] = "DefaultDist";
int instantiation_limit = 256;
char mainModuleName[256] = "";
bool printSearchDirs = false;
bool printModuleFiles = false;

Map<const char*, const char*> configParamMap;
bool debugCCode = false;
bool optimizeCCode = false;

bool fEnableTimers = false;
Timer timer1;
Timer timer2;
Timer timer3;
Timer timer4;
Timer timer5;

bool fNoMemoryFrees = false;

int numGlobalsOnHeap = 0;

const char* compileCommand = NULL;
char compileVersion[64];

static bool printCopyright = false;
static bool printHelp = false;
static bool printEnvHelp = false;
static bool printSettingsHelp = false;
static bool printLicense = false;
static bool printVersion = false;


static void setupChplHome(void) {
  const char* chpl_home = getenv("CHPL_HOME");
  if (chpl_home == NULL) {
    USR_FATAL("$CHPL_HOME must be set to run chpl");
  } else {
    strncpy(CHPL_HOME, chpl_home, FILENAME_MAX);
  }
}

static const char* setupEnvVar(const char* varname, const char* script) {
  const char* val = runUtilScript(script);
  configParamMap.put(astr(varname), val);
  return val;
}

#define SETUP_ENV_VAR(varname, script)          \
  varname = setupEnvVar(#varname, script);

//
// Can't rely on a variable initialization order for globals, so any
// variables that need to be initialized in a particular order go here
//
static void setupOrderedGlobals(void) {
  // Set up CHPL_HOME first
  setupChplHome();
  
  // Then CHPL_* variables
  SETUP_ENV_VAR(CHPL_HOST_PLATFORM, "platform.pl --host");
  SETUP_ENV_VAR(CHPL_TARGET_PLATFORM, "platform.pl --target");
  SETUP_ENV_VAR(CHPL_HOST_COMPILER, "compiler.pl --host");
  SETUP_ENV_VAR(CHPL_TARGET_COMPILER, "compiler.pl --target");
  SETUP_ENV_VAR(CHPL_TASKS, "tasks.pl");
  SETUP_ENV_VAR(CHPL_COMM, "comm.pl");

  // These depend on the environment variables being set
  fLocal = !strcmp(CHPL_COMM, "none");
  fSerial = !strcmp(CHPL_TASKS, "none"); 
  // Enable if we are going to use Nvidia's NVCC compiler
  fGPU = !strcmp(CHPL_TARGET_COMPILER, "nvidia");
  // Eventually, we should only generate structural definitions when
  // doing multirealm compilations.  However, we don't have the mechanism
  // in place to support two communication interfaces yet...
  genCommunicatedStructures = (strcmp(CHPL_COMM, "pvm") == 0);
}


static void recordCodeGenStrings(int argc, char* argv[]) {
  compileCommand = astr("chpl");
  for (int i = 1; i < argc; i++) {
    compileCommand = astr(compileCommand, " ", argv[i]);
  }
  get_version(compileVersion);
}


static void setChapelDebug(ArgumentState* arg_state, char* arg_unused) {
  printCppLineno = true;
}

static void setDevelSettings(ArgumentState* arg_state, char* arg_unused) {
  // have to handle both cases since this will be called with --devel
  // and --no-devel
  if (developer) {
    ccwarnings = true;
  } else {
    ccwarnings = false;
  }
}

static void 
handleLibrary(ArgumentState* arg_state, char* arg_unused) {
  addLibInfo(astr("-l", libraryFilename));
}

static void 
handleLibPath(ArgumentState* arg_state, char* arg_unused) {
  addLibInfo(astr("-L", libraryFilename));
}

static void handleIncDir(ArgumentState* arg_state, char* arg_unused) {
  addIncInfo(incFilename);
}

static void
compute_program_name_loc(char* orig_argv0, const char** name, const char** loc) {
  char* argv0 = strdup(orig_argv0);
  char* lastslash = strrchr(argv0, '/');
  if (lastslash == NULL) {
    *name = argv0;
    *loc = ".";   // BLC: this is inaccurate; we should search the path.  
                  // It's no less accurate than what we did previously, though.
  } else {
    *lastslash = '\0';
    *name = lastslash+1;
    *loc = argv0;
  }
}


static void runCompilerInGDB(int argc, char* argv[]) {
  const char* gdbCommandFilename = createGDBFile(argc, argv);
  const char* command = astr("gdb -q ", argv[0]," -x ", gdbCommandFilename);
  int status = mysystem(command, "running gdb", 0);

  clean_exit(status);
}


static void readConfigParam(ArgumentState* arg_state, char* arg_unused) {
  // Expect arg_unused to be a string of either of these forms:
  // 1. name=value -- set the config param "name" to "value"
  // 2. name       -- set the boolean config param "name" to NOT("name")
  //                  if name is not type bool, set it to 0.

  char *name = (char*)astr(arg_unused);
  char *value;
  value = strstr(name, "=");
  if (value) {
    *value = '\0';
    value++;
    if (value[0]) {
      // arg_unused was name=value
      configParamMap.put(astr(name), value);
    } else {
      // arg_unused was name=  <blank>
      USR_FATAL("Missing config param value");
    }
  } else {
    // arg_unused was just name
    configParamMap.put(astr(name), "");
  }
}


static void addModulePath(ArgumentState* arg_state, char* newpath) {
  addUserModulePath(newpath);
}

Vec<const char*> realms;

int32_t getNumRealms(void) {
  INT_ASSERT(realms.n);
  return (int32_t)realms.n;
}

static void processRealmArgs(void) {
  const char* allrealms = configParamMap.get(astr("realmTypes"));
  if (allrealms == NULL) {
    realms.add(CHPL_TARGET_PLATFORM);
  } else {
    const char* start = allrealms;
    int numRealms = 0;
    const char* space;
    do {
      space = strchr(start, ' ');
      if (space == NULL) {
        realms.add(start);
      } else {
        int len = space-start+1;
        //        printf("len = %d\n", len);
        char* realmStr = (char*)malloc(sizeof(char)*len);
        strncpy(realmStr, start, len);
        realmStr[len-1] = '\0';
        //        printf("realmstr = '%s'\n", realmStr);
        realms.add(realmStr);
        start = space+1;
      }
      numRealms += 1;
    } while (space != NULL);
    numRealms = 0;
    forv_Vec(const char*, realm, realms) {
      //      printf("realm %d = %s\n", numRealms, realm);
      numRealms += 1;
    }
  }
}


static void noteCppLinesSet(ArgumentState* arg, char* unused) {
  userSetCppLineno = true;
}


static void verifySaveCDir(ArgumentState* arg, char* unused) {
  if (saveCDir[0] == '-') {
    USR_FATAL("--savec takes a directory name as its argument\n"
              "       (you specified '%s', assumed to be another flag)",
              saveCDir);
  }
}


static void turnOffChecks(ArgumentState* arg, char* unused) {
  fNoNilChecks = true;
  fNoBoundsChecks = true;
  fNoLocalChecks = true;
}

static void setFastFlag(ArgumentState* arg, char* unused) {
  //
  // Enable all compiler optimizations, disable all runtime checks
  //
  fBaseline = false;
  fieeefloat = false;
  fNoCopyPropagation = false;
  fNoDeadCodeElimination = false;
  fNoInline = false;
  fNoInlineIterators = false;
  fNoOptimizeLoopIterators = false;
  fNoLiveAnalysis = false;
  fNoRemoteValueForwarding = false;
  fNoRemoveCopyCalls = false;
  fNoScalarReplacement = false;
  fNoPrivatization = false;
  fNoChecks = true;
  fNoBoundsChecks = true;
  fNoLocalChecks = true;
  fNoNilChecks = true;
  fNoOptimizeOnClauses = false;
  optimizeCCode = true;
}

static void setBaselineFlag(ArgumentState* arg, char* unused) {
  //
  // disable all chapel compiler optimizations
  //
  fBaseline = true;
  fNoCopyPropagation = true;
  fNoDeadCodeElimination = true;
  fNoInline = true;
  fNoInlineIterators = true;
  fNoLiveAnalysis = true;
  fNoOptimizeLoopIterators = true;
  fNoRemoteValueForwarding = true;
  fNoRemoveCopyCalls = true;
  fNoScalarReplacement = true;
  fNoPrivatization = true;
  fNoOptimizeOnClauses = true;
  fConditionalDynamicDispatchLimit = 0;
}

static void setHelpTrue(ArgumentState* arg, char* unused) {
  printHelp = true;
}  


/*
Flag types:

  I = int
  P = path
  S = string
  D = double
  f = set to false
  F = set to true
  + = increment
  T = toggle
  L = int64 (long)
  N = --no-... flag, --no version sets to false
  n = --no-... flag, --no version sets to true
*/

static ArgumentDescription arg_desc[] = {
  {"", ' ', NULL, "Module Processing Options", NULL, NULL, NULL, NULL},
  {"module-dir", 'M', "<directory>", "Add directory to module search path", "P", moduleSearchPath, NULL, addModulePath},
  {"main-module", ' ', "<module>", "Specify entry point module", "S256", mainModuleName, NULL, NULL},
  {"print-search-dirs", ' ', "", "Print module search path", "F", &printSearchDirs, "CHPL_PRINT_SEARCH_DIRS", NULL},
  {"print-module-files", ' ', "", "Print module file locations", "F", &printModuleFiles, NULL, NULL},
  {"count-tokens", ' ', NULL, "Count tokens in main modules", "F", &countTokens, "CHPL_COUNT_TOKENS", NULL},
  {"print-code-size", ' ', NULL, "Print code size of main modules", "F", &printTokens, "CHPL_PRINT_TOKENS", NULL},

 {"", ' ', NULL, "Parallelism Control Options", NULL, NULL, NULL, NULL},
 {"local", ' ', NULL, "Target one [many] locale[s]", "N", &fLocal, "CHPL_LOCAL", NULL},
 {"serial", ' ', NULL, "[Don't] Serialize parallel constructs", "N", &fSerial, "CHPL_SERIAL", NULL},
 {"serial-forall", ' ', NULL, "[Don't] Serialize forall constructs", "N", &fSerialForall, "CHPL_SERIAL_FORALL", NULL},

 {"", ' ', NULL, "Optimization Control Options", NULL, NULL, NULL, NULL},
 {"baseline", ' ', NULL, "Disable all Chapel optimizations", "F", &fBaseline, "CHPL_BASELINE", setBaselineFlag},
  {"conditional-dynamic-dispatch-limit", ' ', "<limit>", "Set limit to use conditionals for dynamic dispatch", "I", &fConditionalDynamicDispatchLimit, "CHPL_CONDITIONAL_DYNAMIC_DISPATCH_LIMIT", NULL},
 {"fast", ' ', NULL, "Use fast default settings", "F", &fFastFlag, NULL, setFastFlag},
 {"ieee-float", ' ', NULL, "Generate code that is strict [lax] with respect to IEEE compliance", "N", &fieeefloat, "CHPL_IEEE_FLOAT", NULL},
 {"copy-propagation", ' ', NULL, "Enable [disable] copy propagation", "n", &fNoCopyPropagation, "CHPL_DISABLE_COPY_PROPAGATION", NULL},
 {"dead-code-elimination", ' ', NULL, "Enable [disable] dead code elimination", "n", &fNoDeadCodeElimination, "CHPL_DISABLE_DEAD_CODE_ELIMINATION", NULL},
 {"inline", ' ', NULL, "Enable [disable] function inlining", "n", &fNoInline, NULL, NULL},
 {"inline-iterators", ' ', NULL, "Enable [disable] iterator inlining", "n", &fNoInlineIterators, "CHPL_DISABLE_INLINE_ITERATORS", NULL},
 {"live-analysis", ' ', NULL, "Enable [disable] live variable analysis", "n", &fNoLiveAnalysis, "CHPL_DISABLE_LIVE_ANALYSIS", NULL},
 {"optimize-loop-iterators", ' ', NULL, "Enable [disable] optimization of iterators composed of a single loop", "n", &fNoOptimizeLoopIterators, "CHPL_DISABLE_OPTIMIZE_LOOP_ITERATORS", NULL},
 {"optimize-on-clauses", ' ', NULL, "Enable [disable] optimization of on clauses", "n", &fNoOptimizeOnClauses, "CHPL_DISABLE_OPTIMIZE_ON_CLAUSES", NULL},
 {"optimize-on-clause-limit", ' ', "<limit>", "Limit recursion depth of on clause optimization search", "I", &optimize_on_clause_limit, "CHPL_OPTIMIZE_ON_CLAUSE_LIMIT", NULL},
 {"privatization", ' ', NULL, "Enable [disable] privatization of distributed arrays and domains", "n", &fNoPrivatization, "CHPL_DISABLE_PRIVATIZATION", NULL},
 {"remote-value-forwarding", ' ', NULL, "Enable [disable] remote value forwarding", "n", &fNoRemoteValueForwarding, "CHPL_DISABLE_REMOTE_VALUE_FORWARDING", NULL},
 {"remove-copy-calls", ' ', NULL, "Enable [disable] remove copy calls", "n", &fNoRemoveCopyCalls, "CHPL_DISABLE_REMOVE_COPY_CALLS", NULL},
 {"scalar-replacement", ' ', NULL, "Enable [disable] scalar replacement", "n", &fNoScalarReplacement, "CHPL_DISABLE_SCALAR_REPLACEMENT", NULL},

 {"", ' ', NULL, "Run-time Semantic Check Options", NULL, NULL, NULL, NULL},
 {"no-checks", ' ', NULL, "Disable all following checks", "F", &fNoChecks, "CHPL_NO_CHECKS", turnOffChecks},
 {"bounds-checks", ' ', NULL, "Enable [disable] bounds checking", "n", &fNoBoundsChecks, "CHPL_NO_BOUNDS_CHECKING", NULL},
 {"local-checks", ' ', NULL, "Enable [disable] local block checking", "n", &fNoLocalChecks, NULL, NULL},
 {"nil-checks", ' ', NULL, "Enable [disable] nil checking", "n", &fNoNilChecks, "CHPL_NO_NIL_CHECKS", NULL},

 {"", ' ', NULL, "C Code Generation Options", NULL, NULL, NULL, NULL},
 {"cpp-lines", ' ', NULL, "[Don't] Generate #line annotations", "N", &printCppLineno, "CHPL_CG_CPP_LINES", noteCppLinesSet},
 {"savec", ' ', "<directory>", "Save generated C code in directory", "P", saveCDir, "CHPL_SAVEC_DIR", verifySaveCDir},

 {"", ' ', NULL, "C Code Compilation Options", NULL, NULL, NULL, NULL},
 {"ccflags", ' ', "<flags>", "Back-end C compiler flags", "S256", ccflags, "CHPL_CC_FLAGS", NULL},
 {"ldflags", ' ', "<flags>", "Back-end C linker flags", "S256", ldflags, "CHPL_LD_FLAGS", NULL},
  {"hdr-search-path", 'I', "<directory>", "C header search path", "P", incFilename, NULL, handleIncDir},
 {"lib-linkage", 'l', "<library>", "C library linkage", "P", libraryFilename, "CHPL_LIB_NAME", handleLibrary},
 {"lib-search-path", 'L', "<directory>", "C library search path", "P", libraryFilename, "CHPL_LIB_PATH", handleLibPath},
 {"make", ' ', "<make utility>", "Make utility for generated code", "S256", &chplmake, "CHPL_MAKE", NULL},
 {"debug", 'g', NULL, "[Don't] Support debugging of generated C code", "N", &debugCCode, "CHPL_DEBUG", setChapelDebug},
 {"optimize", 'O', NULL, "[Don't] Optimize generated C code", "N", &optimizeCCode, "CHPL_OPTIMIZE", NULL},
 {"output", 'o', "<filename>", "Name output executable", "P", executableFilename, "CHPL_EXE_NAME", NULL},

 {"", ' ', NULL, "Compilation Trace Options", NULL, NULL, NULL, NULL},
 {"print-commands", ' ', NULL, "Print system commands", "F", &printSystemCommands, "CHPL_PRINT_COMMANDS", NULL},
 {"print-passes", ' ', NULL, "Print compiler passes", "F", &printPasses, "CHPL_PRINT_PASSES", NULL},

 {"", ' ', NULL, "Miscellaneous Options", NULL, NULL, NULL, NULL},
 {"devel", ' ', NULL, "Compile as a developer [user]", "N", &developer, "CHPL_DEVELOPER", setDevelSettings},
 {"explain-call", ' ', "<call>[:<module>][:<line>]", "Explain resolution of call", "S256", fExplainCall, NULL, NULL},
 {"explain-instantiation", ' ', "<function|type>[:<module>][:<line>]", "Explain instantiation of type", "S256", fExplainInstantiation, NULL, NULL},
 {"instantiate-max", ' ', "<max>", "Limit number of instantiations", "I", &instantiation_limit, "CHPL_INSTANTIATION_LIMIT", NULL},
 {"no-warnings", ' ', NULL, "Disable output of warnings", "F", &ignore_warnings, "CHPL_DISABLE_WARNINGS", NULL},
 {"set", 's', "<name>[=<value>]", "Set config param value", "S", NULL, NULL, readConfigParam},

 {"", ' ', NULL, "Compiler Information Options", NULL, NULL, NULL, NULL},
 {"copyright", ' ', NULL, "Show copyright", "F", &printCopyright, NULL},
 {"help", 'h', NULL, "Help (show this list)", "F", &printHelp, NULL},
 {"help-env", ' ', NULL, "Environment variable help", "F", &printEnvHelp, "", setHelpTrue},
 {"help-settings", ' ', NULL, "Current flag settings", "F", &printSettingsHelp, "", setHelpTrue},
 {"license", ' ', NULL, "Show license", "F", &printLicense, NULL},
 {"version", ' ', NULL, "Show version", "F", &printVersion, NULL},

 {"", ' ', NULL, "Developer Flags", NULL, NULL, NULL, NULL},
 {"", ' ', NULL, "Debug Output", NULL, NULL, NULL, NULL},
 {"cc-warnings", ' ', NULL, "[Don't] Give warnings for generated code", "N", &ccwarnings, "CHPL_CC_WARNINGS", NULL},
 {"c-line-numbers", ' ', NULL, "Use C code line numbers and filenames", "F", &fCLineNumbers, NULL, NULL},
 {"gen-ids", ' ', NULL, "Pepper generated code with BaseAST::id numbers", "F", &fGenIDS, "CHPL_GEN_IDS", NULL},
 {"html", 't', NULL, "Dump IR in HTML", "T", &fdump_html, "CHPL_HTML", NULL},
 {"log", 'd', "[a|i|F|d|s]", "Specify debug logs", "S512", log_flags, "CHPL_LOG_FLAGS", log_flags_arg},
 {"log-dir", ' ', "<path>", "Specify log directory", "P", log_dir, "CHPL_LOG_DIR", NULL},
 {"parser-debug", 'D', NULL, "Set parser debug level", "+", &debugParserLevel, "CHPL_PARSER_DEBUG", NULL},
 {"print-dispatch", ' ', NULL, "Print dynamic dispatch table", "F", &fPrintDispatch, NULL, NULL},
 {"print-statistics", ' ', "[n|k|t]", "Print AST statistics", "S256", fPrintStatistics, NULL, NULL},
 {"report-inlining", ' ', NULL, "Print inlined functions", "F", &report_inlining, NULL, NULL},

 {"", ' ', NULL, "Misc. Developer Flags", NULL, NULL, NULL, NULL},
 {"default-dist", ' ', "<distribution>", "Change the default distribution", "S256", defaultDist, "CHPL_DEFAULT_DIST", NULL},
 {"gdb", ' ', NULL, "Run compiler in gdb", "F", &rungdb, NULL, NULL},
 {"ignore-errors", ' ', NULL, "Attempt to ignore errors", "F", &ignore_errors, "CHPL_IGNORE_ERRORS", NULL},
 {"no-codegen", ' ', NULL, "Suppress code generation", "F", &no_codegen, "CHPL_NO_CODEGEN", NULL},
 {"memory-frees", ' ', NULL, "Enable [disable] memory frees in the generated code", "n", &fNoMemoryFrees, "CHPL_DISABLE_MEMORY_FREES", NULL},
 {"runtime", ' ', NULL, "compile Chapel runtime file", "F", &fRuntime, NULL, NULL},
 {"timers", ' ', NULL, "Enable general timers one to five", "F", &fEnableTimers, "CHPL_ENABLE_TIMERS", NULL},
 {"warn-promotion", ' ', NULL, "Warn about scalar promotion", "F", &fWarnPromotion, NULL, NULL},
 {"gen-communicated-structures", ' ', NULL, "Generate type/offset information for potential use in communications", "N", &genCommunicatedStructures, NULL, NULL},
 {0}
};


static ArgumentState arg_state = {
  0, 0,
  "program", 
  "path",
  arg_desc
};


static void setupDependentVars(void) {
  if (developer && !userSetCppLineno) {
    printCppLineno = false;
  }
  processRealmArgs();
}


static void printStuff(void) {
  bool shouldExit = false;
  bool printedSomething = false;

  if (printVersion) {
    fprintf(stdout, "%s Version %s\n", arg_state.program_name, compileVersion);
    printCopyright = true;
    printedSomething = true;
    shouldExit = true;
  }
  if (printLicense) {
    fprintf(stdout,
#include "LICENSE"
            );
    printCopyright = false;
    shouldExit = true;
    printedSomething = true;
  }
  if (printCopyright) {
    fprintf(stdout,
#include "COPYRIGHT"
            );
    printedSomething = true;
  }
  if (printHelp || (!printedSomething && arg_state.nfile_arguments < 1)) {
    if (printedSomething) printf("\n");
    usage(&arg_state, (printHelp == false), printEnvHelp, printSettingsHelp);
    shouldExit = true;
    printedSomething = true;
  }
  if (printedSomething && arg_state.nfile_arguments < 1) {
    shouldExit = true;
  }
  if (shouldExit) {
    clean_exit(0);
  }
}


static void
compile_all(void) {
  initFlags();
  initPrimitive();
  initPrimitiveTypes();
  testInputFiles(arg_state.nfile_arguments, arg_state.file_argument);
  runPasses();
}

int main(int argc, char *argv[]) {
  setupOrderedGlobals();
  compute_program_name_loc(argv[0], &(arg_state.program_name),
                           &(arg_state.program_loc));
  process_args(&arg_state, argc, argv);
  setupDependentVars();
  setupModulePaths();
  recordCodeGenStrings(argc, argv);
  startCatchingSignals();
  printStuff();
  if (rungdb)
    runCompilerInGDB(argc, argv);
  if (fdump_html || strcmp(log_flags, ""))
    init_logs();
  compile_all();
  if (fEnableTimers) {
    printf("timer 1: %8.3lf\n", timer1.elapsed());
    printf("timer 2: %8.3lf\n", timer2.elapsed());
    printf("timer 3: %8.3lf\n", timer3.elapsed());
    printf("timer 4: %8.3lf\n", timer4.elapsed());
    printf("timer 5: %8.3lf\n", timer5.elapsed());
  }
  free_args(&arg_state);
  clean_exit(0);
  return 0;
}
