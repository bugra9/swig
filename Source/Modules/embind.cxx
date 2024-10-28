#include "swigmod.h"
#include "cparse.h"
#include <ctype.h>
#include <iostream>

static const char *usage = (char *) "";

int findStartOfParenthesis(std::string s) {
  bool start = false;
  int count = 0;
  for (int i = s.length() - 1; i >= 0; i -= 1) {
    if (!start && s[i] == ')') {
      start = true;
      count = 1;
    } else if (start && s[i] == ')') {
      count += 1;
    } else if (start && s[i] == '(') {
      count -= 1;
      if (count == 0) {
        return i;
      }
    }
  }
  return -1;
}

int findEndOfParenthesisOrBlank(std::string s) {
  for (int i = s.length() - 1; i >= 0; i -= 1) {
    if (s[i] == ')' || s[i] == ' ') {
      return i;
    }
  }
  return -1;
}

class EMBIND:public Language {
public:
  File *f_begin;
  File *f_exports;
  File *f_runtime;
  File *f_cxx_header;
  File *f_cxx_wrapper;
  File *f_cxx_functions;

  String *exports;
  String *module;
  virtual void main(int argc, char *argv[]);
  virtual int top(Node *n);
  virtual int functionWrapper(Node *n);
  virtual int variableWrapper(Node *n);
  virtual int constantWrapper(Node *n);
  //  virtual int classDeclaration(Node *n);
  virtual int enumDeclaration(Node *n);
  virtual int typedefHandler(Node *n);

  //c++ specific code
  virtual int constructorHandler(Node *n);
  virtual int destructorHandler(Node *n);
  virtual int memberfunctionHandler(Node *n);
  virtual int membervariableHandler(Node *n);
  virtual int classHandler(Node *n);

private:

};

void EMBIND::main(int argc, char *argv[]) {
  int i;

  Preprocessor_define("SWIGEMBIND 1", 0);
  SWIG_library_directory("embind");
  SWIG_config_file("embind.swg");

  allow_overloading();
}

int EMBIND::top(Node *n) {
  module = Getattr(n, "name");

  String *cxx_filename = Getattr(n, "outfile");
  String *cxx_exports_filename = NewString(cxx_filename);
  Printf(cxx_exports_filename, ".exports.json");

  f_begin = NewFile(cxx_filename, "w", SWIG_output_files());
  f_exports = NewFile(cxx_exports_filename, "w", SWIG_output_files());
  if (!f_begin) {
    Printf(stderr, "Unable to open %s for writing\n", cxx_filename);
    Exit(EXIT_FAILURE);
  }
  if (!f_exports) {
    Printf(stderr, "Unable to open %s for writing\n", cxx_exports_filename);
    Exit(EXIT_FAILURE);
  }

  f_runtime = NewString("");
  f_cxx_header = NewString("");
  f_cxx_wrapper = NewString("");
  f_cxx_functions = NewString("");
  exports = NewString("");

  Printf(exports, "[");
  Printf(f_cxx_header, "#include <emscripten/bind.h>\n");

  Swig_register_filebyname("header", f_cxx_header);
  Swig_register_filebyname("wrapper", f_cxx_wrapper);
  Swig_register_filebyname("begin", f_begin);
  Swig_register_filebyname("runtime", f_runtime);

  Swig_banner(f_begin);

  Language::top(n);

  Printf(f_cxx_wrapper, "EMSCRIPTEN_BINDINGS(Functions) {\n");
  Printf(f_cxx_wrapper, "%s", f_cxx_functions);
  Printf(f_cxx_wrapper, "}\n");

  Printf(exports, "]\n");
  Replace(exports, ", ", "", DOH_REPLACE_FIRST);

  Dump(exports, f_exports);
  Dump(f_cxx_header, f_runtime);
  Dump(f_cxx_wrapper, f_runtime);
  Dump(f_runtime, f_begin);
  Delete(f_runtime);
  Delete(f_begin);
  Delete(f_cxx_header);
  Delete(f_cxx_wrapper);
  Delete(f_cxx_functions);
  Delete(exports);
  Delete(f_exports);

  return SWIG_OK;
}

int EMBIND::classHandler(Node *n) {
  String *name = Getattr(n, "sym:name");
  String *nsname = Getattr(n, "name");
  String *kind = Getattr(n, "kind");
  String *bases = Getattr(n, "bases");
  String *classType = Getattr(n, "classtype");

  bool isListener = Len(name) > 8 && std::string(Char(name)).substr(Len(name) - 8) == "Listener";

  if (std::string(Char(classType)).substr(0, 12) == "std::vector<") {
    String *params = NewString(std::string(Char(classType)).substr(5).c_str());
    Printf(f_cxx_wrapper, "EMSCRIPTEN_BINDINGS(%s) {\n  emscripten::register_%s(\"%s\");\n}\n\n", name, params, name);
    Printf(exports, ", \"%s\"", name);
    
  } else if (std::string(Char(classType)).substr(0, 9) == "std::map<") {
    String *params = NewString(std::string(Char(classType)).substr(5).c_str());
    Printf(f_cxx_wrapper, "EMSCRIPTEN_BINDINGS(%s) {\n  emscripten::register_%s(\"%s\");\n}\n\n", name, params, name);
    Printf(exports, ", \"%s\"", name);
  } else {
    if (isListener) {
      String *className = NewString(name);
      String *listenerHeader = NewString("");
      String *listenerBody = NewString("");
      String *listenerCall = NewString("");
      Printf(listenerHeader, "#include <emscripten/threading.h>\n#include <emscripten/proxying.h>\n\ntypedef union em_variant_val { int i; int64_t i64; float f; double d; void *vp; char *cp; } em_variant_val;\n#define EM_QUEUED_CALL_MAX_ARGS 11\ntypedef struct em_queued_call { int functionEnum; void *functionPtr; _Atomic uint32_t operationDone; em_variant_val args[EM_QUEUED_JS_CALL_MAX_ARGS]; em_variant_val returnValue; void *satelliteData; int calleeDelete; } em_queued_call;\n");
      Printf(listenerHeader, "\nclass EmRunOnMainThread {\npublic:\n");
      Printf(listenerCall, "struct %sWrapper : public emscripten::wrapper<%s> {\n  EMSCRIPTEN_WRAPPER(%sWrapper);\n", name, nsname, name);
      
      for (Node *c = firstChild(n); c; c = nextSibling(c)) {
        String *returnType = Getattr(c, "type");
        Replace(returnType, "(", "", DOH_REPLACE_ANY);
        Replace(returnType, ")", "", DOH_REPLACE_ANY);
        
        String *name = Getattr(c, "name");
        if (returnType) {
          String *params = NewString("");
          String *variables = NewString("");
          String *variables2 = NewString("");
          String *variables3 = NewString("");
          ParmList *pl = Getattr(c, "parms");
          int argnum = 0;
          for (Parm *p = pl; p; p = nextSibling(p), argnum++) {
            String *pName = Getattr(p, "name");
            String *pTypeSplit = Split(Getattr(p, "type"), '.', -1);
            String *pType = Getitem(pTypeSplit, Len(pTypeSplit) - 1);
            Replace(pType, "(", "", DOH_REPLACE_ANY);
            Replace(pType, ")", "", DOH_REPLACE_ANY);
            String *pTypeAll = NewString("");
            String *pTypeAll2 = NewString("");
            if (Len(pTypeSplit) > 1 && Strcmp(Getitem(pTypeSplit, Len(pTypeSplit) - 2), "q(const)") == 0) {
              Printf(pTypeAll, "const %s& %s", pType, pName);
              Printf(pTypeAll2, "const %s", pType);
            } else {
              Printf(pTypeAll, "%s %s", pType, pName);
              Printf(pTypeAll2, "%s", pType);
            }

            if (argnum == 0) {
              Printf(params, "%s", pTypeAll);
            } else {
              Printf(params, ", %s", pTypeAll);
            }
            Printf(variables, ", %s", pName);
            Printf(variables2, "    call.args[%d].vp = (void *)&%s;\n", argnum + 1, pName);
            Printf(variables3, "    %s %s = *((%s*) q->args[%d].vp);\n", pTypeAll2, pName, pTypeAll2, argnum + 1);
            // Printf(f_cxx_wrapper, "%s\n", p);
          }

          String *returnBody = NewString("");
          String *returnCall = NewString("");
          String *returnCallEnd = NewString("");
          Printf(listenerHeader, "  static void %s (void* arg);\n", name);
          if (Strcmp(returnType, "void") != 0) {
            Printf(listenerHeader, "  struct Return%s {\n    Return%s(%s value) {\n      this->value = std::move(value);\n    }\n    %s value;\n  };\n  struct Return%sContainer {\n    std::shared_ptr<Return%s> value;\n  };\n", name, name, returnType, returnType, name, name );
            Printf(returnBody, "    Return%sContainer r = *((Return%sContainer*) q->returnValue.vp);\n    r.value = std::shared_ptr<Return%s>(new Return%s(self->call<%s>(\"%s\"%s)));\n", name, name, name, name, returnType, name, variables);
            Printf(returnCall, "    EmRunOnMainThread::Return%sContainer r;\n    call.returnValue.vp = (void *)&r;\n", name);
            Printf(returnCallEnd, "    return r.value->value;\n");
          } else {
            Printf(returnBody, "    self->call<void>(\"%s\"%s);\n", name, variables);
          }
          // Printf(listenerCall, "  %s %s(%s) { return call<%s>(\"%s\"%s); }\n", returnType, name, params, returnType, name, variables);
          Printf(listenerCall, "  %s %s(%s) {\n    em_queued_call call = {EM_FUNC_SIG_V};\n%s\n    call.args[0].vp = (void *)this;\n%s\n    if (pthread_equal(emscripten_main_browser_thread_id(), pthread_self())) {\n      EmRunOnMainThread::%s(&call);\n    } else {\n      emscripten_proxy_async(emscripten_proxy_get_system_queue(), emscripten_main_browser_thread_id(), EmRunOnMainThread::%s, &call);\n      emscripten_wait_for_call_v(&call, INFINITY);\n    }\n%s  }\n\n", returnType, name, params, returnCall, variables2, name, name, returnCallEnd);
          Printf(listenerBody, "void EmRunOnMainThread::%s (void* arg) {\n    em_queued_call* q = (em_queued_call*)arg;\n    %sWrapper* self = (%sWrapper*) q->args[0].vp;\n%s\n%s    q->operationDone = 1;\n    emscripten_futex_wake(&q->operationDone, INT_MAX);\n}\n", name, className, className, variables3, returnBody);
        }
      }

      Printf(listenerHeader, "};\n");
      Printf(listenerCall, "};\n");
      Printf(f_cxx_wrapper, "%s\n\n%s\n\n%s\n", listenerHeader, listenerCall, listenerBody);
    }

    String *super = NewString("");
    if (bases) {
      Printf(super, ", emscripten::base<%s>", Getattr(First(bases).item, "name"));
    }

    Printf(f_cxx_wrapper, "EMSCRIPTEN_BINDINGS(%s) {\n  emscripten::class_<%s%s>(\"%s\")\n", name, nsname, super, name);
    Printf(exports, ", \"%s\"", name);
    if (Getattr(n, "feature:shared_ptr")) Printf(f_cxx_wrapper, "    .smart_ptr<std::shared_ptr<%s>>(\"%s\")\n", nsname, name);
    if (isListener) Printf(f_cxx_wrapper, "    .allow_subclass<%sWrapper, std::shared_ptr<%sWrapper>>(\"%sWrapper\", \"%sWrapperSharedPtr\")\n", name, name, name, name);
    Language::classHandler(n);
    Printf(f_cxx_wrapper, "  ;\n}\n\n");
  }

  return SWIG_OK;
}

int EMBIND::constructorHandler(Node *n) {
  Language::constructorHandler(n);
  return SWIG_OK;
}

int EMBIND::destructorHandler(Node *n) {
  Language::destructorHandler(n);
  return SWIG_OK;
}

int EMBIND::memberfunctionHandler(Node *n) {
  return Language::memberfunctionHandler(n);
}

int EMBIND::membervariableHandler(Node *n) {
  return Language::membervariableHandler(n);
}

int EMBIND::functionWrapper(Node *n) {
  String   *bname   = Getattr(n, "sym:name");
  SwigType *btype   = Getattr(n, "type");
  ParmList *bparms  = Getattr(n, "parms");
  String   *bparmstr= ParmList_str_defaultargs(bparms); // to string
  String   *bfunc   = SwigType_str(btype, NewStringf("%s(%s)", bname, bparmstr));

  String *wrap = Getattr(n, "wrap:code");
  String *actioncode = emit_action(n);
  // Printf(f_cxx_wrapper, "\n\na - %s\n", wrap);

  auto actioncodeString = std::string(Char(actioncode));
  int i = findStartOfParenthesis(actioncodeString);
  if (i > 0) {
    actioncodeString = actioncodeString.substr(0, i);
  }
  i = findEndOfParenthesisOrBlank(actioncodeString);
  if (i > 0) {
    actioncodeString = actioncodeString.substr(i + 1);
  }

  ParmList *parms = Getattr(n, "parms");
  String *name = Getattr(n, "name");
  String *name2 = Getattr(n, "memberfunctionHandler:sym:name");
  String *staticName = Getattr(n, "staticmemberfunctionHandler:name");
  String *variableName = Getattr(n, "membervariableHandler:sym:name");
  String *type = Getattr(n, "nodeType");
  String *view = Getattr(n, "view");

  String *qualifier = Getattr(n, "qualifier");
  String *constFunctionStr = NewString("");

  if (qualifier && Strcmp(qualifier, "q(const).") == 0) {
    Printf(constFunctionStr, "const");
  }


  String *returnTypeSplit = Split(Getattr(n, "type"), '.', -1);
  String *returnType = Getitem(returnTypeSplit, Len(returnTypeSplit) - 1);
  Replace(returnType, "(", "", DOH_REPLACE_ANY);
  Replace(returnType, ")", "", DOH_REPLACE_ANY);

  /* if (Strcmp(type, "constructor") != 0 && Strcmp(Getitem(returnTypeSplit, 0), "p") == 0) {
    return SWIG_OK;
  } */

  std::string nameStr = std::string(Char(name));

  if (nameStr.length() > 9 && nameStr.substr(0, 9) == "operator ") {
    return SWIG_OK;
  }


  Node *parent = parentNode(n);
  String *className = Getattr(parent, "name");
  String *parentNodeType = Getattr(parent, "nodeType");
  if (Strcmp(parentNodeType, "extend") == 0) {
    parent = parentNode(parent);
    className = Getattr(parent, "name");
  }

  if (Len(className) != 0 && actioncodeString.substr(0, 2) == "->") {
    actioncodeString = std::string(Char(className)) + "::" + actioncodeString.substr(2);
  }

  String *funcName = NewString(actioncodeString.c_str());
  // Printf(f_cxx_wrapper, "\n\n%s\n", funcName);

  // Printf(f_cxx_wrapper, "\n\n\n%s - %s\n", className, parent);
  // Printf(f_cxx_wrapper, "%s\n", n);
  bool isListener = Len(className) > 8 && std::string(Char(className)).substr(Len(className) - 8) == "Listener";
  bool isUsingSameName = false;

  int order = 0;
  bool order_break = false;

  for (Node *c = firstChild(parent); c; c = nextSibling(c)) {
    String *childName = Getattr(c, "name");
    
    if (!childName) continue;

    if (c == n) {
      order_break = true;
      continue;
    }
    std::string childNameStr = std::string(Char(childName));

    std::string realNameStr = "";
    if (Len(name) != 0 && Len(name2) != 0) {
      realNameStr = nameStr;
    } else if (Len(staticName) != 0) {
      realNameStr = std::string(Char(staticName));
    }

    if (realNameStr == "") continue;

    if (childNameStr == realNameStr) {
      isUsingSameName = true;
      if (!order_break) order += 1;
    }
  }
  
  String *smartPtrParams = NewString("");
  String *constructorParams = NewString("");
  {
    ParmList *pl = Getattr(n, "parms");
    int argnum = 0;
    for (Parm *p = pl; p; p = nextSibling(p), argnum++) {
      String *tempPType = Getattr(p, "type");
      std::string tempPTypeStr = std::string(Char(tempPType));

      /* if (Strcmp(type, "constructor") == 0 || Len(staticName) != 0 || argnum > 0) {
        if (tempPTypeStr.length() > 2 && tempPTypeStr.substr(0, 2) == "p.") {
          return SWIG_OK;
        }
      } */

      String *pTypeSplit = Split(tempPType, '.', -1);
      String *pType = Getitem(pTypeSplit, Len(pTypeSplit) - 1);
      Replace(pType, "(", "", DOH_REPLACE_ANY);
      Replace(pType, ")", "", DOH_REPLACE_ANY);

      if (argnum == 0) Printf(constructorParams, "%s", pType);
      else Printf(constructorParams, ", %s", pType);

      Printf(smartPtrParams, ", %s", pType);
    }
  }

  if (Strcmp(type, "constructor") == 0) {
    if (Getattr(parent, "feature:polymorphic_shared_ptr")) {
      Printf(f_cxx_wrapper, "    .smart_ptr_constructor(\"%s\", &std::make_shared<%s%s>)\n", name, className, smartPtrParams);
    } else {
      Printf(f_cxx_wrapper, "    .constructor<%s>()\n", constructorParams);
    }
  } else if (Len(className) != 0) {
    String *params = NewString("");
    String *variables = NewString("");
    String *types = NewString("");
    ParmList *pl = Getattr(n, "parms");
    int argnum = 0;
    for (Parm *p = pl; p; p = nextSibling(p), argnum++) {
      String *pName = Getattr(p, "name");

      if (Len(pName) == 0) {
        pName = Getattr(p, "lname");
      }

      if (Strcmp(pName, "self") == 0) {
        --argnum;
        continue;
      }

      String *pTypeSplit = Split(Getattr(p, "type"), '.', -1);
      String *pType = Getitem(pTypeSplit, Len(pTypeSplit) - 1);
      Replace(pType, "(", "", DOH_REPLACE_ANY);
      Replace(pType, ")", "", DOH_REPLACE_ANY);
      String *pTypeAll = NewString("");
      if (Len(pTypeSplit) > 1 && Strcmp(Getitem(pTypeSplit, Len(pTypeSplit) - 2), "q(const)") == 0) {
        String *temp = NewString("");
        Printf(temp, "const %s&", pType);
        pType = temp;
      }
      Printf(pTypeAll, "%s %s", pType, pName);

      if (argnum == 0) {
        Printf(variables, "%s", pName);
        Printf(types, "%s", pType);
      } else {
        Printf(variables, ", %s", pName);
        Printf(types, ", %s", pType);
      }
      Printf(params, ", %s", pTypeAll);
    }

    if (Len(variableName) != 0) {

    } else if (Len(staticName) != 0 && Strcmp(view, "destructorHandler") != 0) {
      if (isUsingSameName) {
        Printf(f_cxx_wrapper, "    .class_function(\"%s\", emscripten::select_overload<%s(%s)%s>(&%s))\n", staticName, returnType, types, constFunctionStr, funcName);
      } else {
        Printf(f_cxx_wrapper, "    .class_function(\"%s\", &%s)\n", staticName, funcName);
      }
    } else if (Len(name) != 0 && Strcmp(view, "destructorHandler") != 0) {
      if (Len(name2) != 0) {
        if (isListener) {
          if (Getattr(n, "abstract")) {
            Printf(f_cxx_wrapper, "    .function(\"%s\", &%s::%s, emscripten::pure_virtual())\n", name, className, name);
          } else {
            Printf(f_cxx_wrapper, "    .function(\"%s\", emscripten::optional_override([](%s& self%s) {\n      return self.%s(%s);\n    }))\n", name, className, params, name, variables);
          }
        } else if (isUsingSameName) {
          Printf(f_cxx_wrapper, "    .function(\"%s\", emscripten::select_overload<%s(%s)%s>(&%s))\n", name, returnType, types, constFunctionStr, funcName);
        } else {
          Printf(f_cxx_wrapper, "    .function(\"%s\", &%s)\n", name, funcName);
        }
      } else {
        if (isUsingSameName) {
          Printf(f_cxx_functions, "    emscripten::function(\"%s\", emscripten::select_overload<%s(%s)%s>(&%s));\n", name, returnType, types, constFunctionStr, name);
        } else {
          Printf(f_cxx_functions, "    emscripten::function(\"%s\", &%s);\n", name, name);
        }
        Printf(exports, ", \"%s\"", name);
      }
    }
  }

  return SWIG_OK;
}

int EMBIND::constantWrapper(Node *n) {
  return SWIG_OK;
}

int EMBIND::variableWrapper(Node *n) {
  return SWIG_OK;
}

int EMBIND::typedefHandler(Node *n) {
  return Language::typedefHandler(n);
}

int EMBIND::enumDeclaration(Node *n) {
  if (!ImportMode) {
      if (getCurrentClass() && (cplus_mode != PUBLIC)) return SWIG_NOWRAP;

      String *symname = Getattr(n, "sym:name");
      String *name = Getattr(n, "name");

      Printf(f_cxx_wrapper, "EMSCRIPTEN_BINDINGS(%s) {\n  emscripten::enum_<%s>(\"%s\")\n", symname, name, symname);
      Printf(exports, ", \"%s\"", symname);

      for (Node *c = firstChild(n); c; c = nextSibling(c)) {
        String *cName = Getattr(c, "name");
        String *cValue = Getattr(c, "value");
        Printf(f_cxx_wrapper, "    .value(\"%s\", %s)\n", cName, cValue);
      }

      Printf(f_cxx_wrapper, "    ;\n}\n\n");
  }

  return SWIG_OK;
}

extern "C" Language *swig_embind(void) {
  return new EMBIND();
}
