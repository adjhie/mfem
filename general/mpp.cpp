// Copyright (c) 2010, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-443211. All Rights
// reserved. See file COPYRIGHT for details.
//
// This file is part of the MFEM library. For more information and source code
// availability see http://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License (as published by the Free
// Software Foundation) version 2.1 dated February 1999.
#include <list>
#include <string>
#include <cstring>
#include <ciso646>
#include <cassert>
#include <fstream>
#include <iostream>
#include <algorithm>

using namespace std;

namespace mfem
{

// *****************************************************************************
#define JIT_STR(...) #__VA_ARGS__
#define JIT_STRINGIFY(...) JIT_STR(__VA_ARGS__)
#define DBG(...) { printf("\033[33m"); \
                   printf(__VA_ARGS__);  \
                   printf(" \033[m");fflush(0); }

// *****************************************************************************
// * Hashing, used here as well as embedded in the code
// *****************************************************************************
#define JIT_HASH_COMBINE_ARGS_SRC                                           \
   template <typename T> struct __hash {                                \
      size_t operator()(const T& h) const noexcept {                    \
         return std::hash<T>{}(h); }                                    \
   };                                                                   \
   template <class T> inline                                            \
   size_t hash_combine(const size_t &s, const T &v) noexcept {          \
      return s^(__hash<T>{}(v)+0x9e3779b9ull+(s<<6)+(s>>2));            \
   }                                                                    \
   template<typename T>                                                 \
   size_t hash_args(const size_t &s, const T &t) noexcept {             \
      return hash_combine(s,t);                                         \
   }                                                                    \
   template<typename T, typename... Args>                               \
   size_t hash_args(const size_t &s, const T &f, Args... a) noexcept {  \
      return hash_args(hash_combine(s,f), a...);                        \
   }

// *****************************************************************************
// * Dump the hashing source here to be able to use it
// *****************************************************************************
JIT_HASH_COMBINE_ARGS_SRC

// *****************************************************************************
// * STRUCTS: argument_t, template_t, kernel_t, context_t and error_t
// *****************************************************************************
struct argument_t
{
   int default_value;
   string type, name;
   bool is_ptr = false, is_amp = false, is_const = false,
        is_restrict = false, is_tpl = false, has_default_value = false;
   std::list<int> range;
   bool operator==(const argument_t &a) { return name == a.name; }
   argument_t() {}
   argument_t(string id): name(id) {}
};
typedef std::list<argument_t>::iterator argument_it;

// *****************************************************************************
struct template_t
{
   string args, params;
   string Targs, Tparams;
   list<list<int> > ranges;
   string return_t, signature;
};

// *****************************************************************************
struct forall_t
{
   string e,N,X,Y,Z,body;
};

// *****************************************************************************
struct kernel_t
{
   bool __jit;
   bool __embed;
   bool __forall;
   bool __template;
   bool __single_source;
   string mfem_cxx;           // holds MFEM_CXX
   string mfem_build_flags;   // holds MFEM_BUILD_FLAGS
   string mfem_install_dir;   // holds MFEM_INSTALL_DIR
   string name;               // kernel name
   string space;              // kernel namespace
   // format, arguments and parameter strings for the template
   string Tformat;            // template format, as in printf
   string Targs;              // template arguments, for hash and call
   string Tparams;            // template parameter, for the declaration
   string Tparams_src;        // template parameter from original source
   // arguments and parameter strings for the standard calls
   // we need two kind of arguments because of the '& <=> *' transformation
   string params;
   string args;
   string args_wo_amp;
   string d2u, u2d;           // double to unsigned place holders
   struct template_t tpl;     // source of the instanciated templates
   string embed;              // source of the embed function
   struct forall_t forall;    // source of the lambda forall
};

// *****************************************************************************
struct context_t
{
   kernel_t ker;
   istream& in;
   ostream& out;
   string& file;
   list<argument_t> args;
   int line, block, parenthesis;
public:
   context_t(istream& i, ostream& o, string &f)
      : in(i), out(o), file(f), line(1), block(-2), parenthesis(-2) {}
};

// *****************************************************************************
struct error_t
{
   int line;
   string file;
   const char *msg;
   error_t(int l, string f, const char *m): line(l), file(f), msg(m) {}
};

// *****************************************************************************
int help(char* argv[])
{
   cout << "MFEM preprocessor:";
   cout << argv[0] << " -o output input" << endl;
   return ~0;
}

// *****************************************************************************
const char* strrnc(const char *s, const unsigned char c, int n =1)
{
   size_t len = strlen(s);
   char* p = const_cast<char*>(s)+len-1;
   for (; n; n--,p--,len--)
   {
      for (; len; p--,len--)
         if (*p==c) { break; }
      if (! len) { return NULL; }
      if (n==1) { return p; }
   }
   return NULL;
}

// *****************************************************************************
void check(context_t &pp, const bool test, const char *msg =NULL)
{ if (not test) { throw error_t(pp.line,pp.file,msg); } }

// *****************************************************************************
bool is_newline(const int ch)
{ return static_cast<unsigned char>(ch) == '\n'; }

// *****************************************************************************
bool good(context_t &pp) { pp.in.peek(); return pp.in.good(); }

// *****************************************************************************
char get(context_t &pp) { return static_cast<char>(pp.in.get()); }

// *****************************************************************************
int put(const char c, context_t &pp)
{
   if (is_newline(c)) { pp.line++; }
   if (pp.ker.__embed) { pp.ker.embed += c; }
   // if we are storing the lbody, just save it w/o output
   if (pp.ker.__forall) { pp.ker.forall.body += c; return c;}
   pp.out.put(c);
   return c;
}

// *****************************************************************************
int put(context_t &pp) { return put(get(pp),pp); }

// *****************************************************************************
void skip_space(context_t &pp, string &out)
{
   while (isspace(pp.in.peek())) { out += get(pp); }
}

// *****************************************************************************
void skip_space(context_t &pp, bool keep=true)
{
   while (isspace(pp.in.peek())) { keep?put(pp):get(pp); }
}

// *****************************************************************************
void drop_space(context_t &pp)
{
   while (isspace(pp.in.peek())) { get(pp); }
}

// *****************************************************************************
bool is_comments(context_t &pp)
{
   if (pp.in.peek() != '/') { return false; }
   pp.in.get();
   assert(!pp.in.eof());
   const int c = pp.in.peek();
   pp.in.unget();
   if (c == '/' || c == '*') { return true; }
   return false;
}

// *****************************************************************************
void singleLineComments(context_t &pp, bool keep=true)
{
   while (!is_newline(pp.in.peek())) { keep?put(pp):get(pp); }
}

// *****************************************************************************
void blockComments(context_t &pp, bool keep=true)
{
   for (char c; pp.in.get(c);)
   {
      if (keep) { put(c,pp); }
      if (c == '*' && pp.in.peek() == '/')
      {
         keep?put(pp):get(pp);
         skip_space(pp, keep);
         return;
      }
   }
}

// *****************************************************************************
void comments(context_t &pp, bool keep=true)
{
   if (not is_comments(pp)) { return; }
   keep?put(pp):get(pp);
   if (keep?put(pp):get(pp) == '/') { return singleLineComments(pp,keep); }
   return blockComments(pp,keep);
}

// *****************************************************************************
void next(context_t &pp, bool keep=true)
{
   keep?skip_space(pp):drop_space(pp);
   comments(pp,keep);
}

// *****************************************************************************
void drop(context_t &pp)
{
   next(pp, false);
}

// *****************************************************************************
bool is_id(context_t &pp)
{
   const int c = pp.in.peek();
   return isalnum(c) or c == '_';
}

// *****************************************************************************
bool is_semicolon(context_t &pp)
{
   skip_space(pp);
   const int c = pp.in.peek();
   return c == ';';
}

// *****************************************************************************
string get_id(context_t &pp)
{
   string id;
   check(pp,is_id(pp),"name w/o alnum 1st letter");
   while (is_id(pp)) { id += get(pp); }
   return id;
}

// *****************************************************************************
bool is_digit(context_t &pp)
{ return isdigit(static_cast<char>(pp.in.peek())); }

// *****************************************************************************
int get_digit(context_t &pp)
{
   string digit;
   check(pp,is_digit(pp),"unknown number");
   while (is_digit(pp)) { digit += get(pp); }
   return atoi(digit.c_str());
}

// *****************************************************************************
string peekn(context_t &pp, const int n)
{
   int k = 0;
   assert(n<64);
   static char c[64];
   for (k=0; k<=n; k++) { c[k] = 0; }
   for (k=0; k<n && good(pp); k++) { c[k] = get(pp); }
   string rtn(c);
   assert(!pp.in.fail());
   for (int l=0; l<k; l++) { pp.in.unget(); }
   return rtn;
}

// *****************************************************************************
string peekid(context_t &pp)
{
   int k = 0;
   const int n = 64;
   static char c[64];
   for (k=0; k<n; k++) { c[k] = 0; }
   for (k=0; k<n; k++)
   {
      if (! is_id(pp)) { break; }
      c[k] = get(pp);
      assert(not pp.in.eof());
   }
   string rtn(c);
   for (int l=0; l<k; l+=1) { pp.in.unget(); }
   return rtn;
}

// *****************************************************************************
void drop_name(context_t &pp)
{ while (is_id(pp)) { get(pp); } }

// *****************************************************************************
bool isvoid(context_t &pp)
{
   skip_space(pp);
   const string void_peek = peekn(pp,4);
   assert(not pp.in.eof());
   if (void_peek == "void") { return true; }
   return false;
}

// *****************************************************************************
bool isnamespace(context_t &pp)
{
   skip_space(pp);
   const string namespace_peek = peekn(pp,2);
   assert(not pp.in.eof());
   if (namespace_peek == "::") { return true; }
   return false;
}

// *****************************************************************************
bool isstatic(context_t &pp)
{
   skip_space(pp);
   const string void_peek = peekn(pp,6);
   assert(not pp.in.eof());
   if (void_peek == "static") { return true; }
   return false;
}

// *****************************************************************************
bool istemplate(context_t &pp)
{
   skip_space(pp);
   const string void_peek = peekn(pp,8);
   assert(not pp.in.eof());
   if (void_peek == "template") { return true; }
   return false;
}

// *****************************************************************************
bool is_star(context_t &pp)
{
   skip_space(pp);
   if (pp.in.peek() == '*') { return true; }
   return false;
}

// *****************************************************************************
bool is_amp(context_t &pp)
{
   skip_space(pp);
   if (pp.in.peek() == '&') { return true; }
   return false;
}

// *****************************************************************************
bool is_left_parenthesis(context_t &pp)
{
   skip_space(pp);
   if (pp.in.peek() == '(') { return true; }
   return false;
}

// *****************************************************************************
bool is_right_parenthesis(context_t &pp)
{
   skip_space(pp);
   if (pp.in.peek() == ')') { return true; }
   return false;
}

// *****************************************************************************
bool is_coma(context_t &pp)
{
   skip_space(pp);
   if (pp.in.peek() == ',') { return true; }
   return false;
}

// *****************************************************************************
bool is_eq(context_t &pp)
{
   skip_space(pp);
   if (pp.in.peek() == '=') { return true; }
   return false;
}

// *****************************************************************************
// * MFEM_JIT
// *****************************************************************************
void jitHeader(context_t &pp)
{
   pp.out << "#include \"general/jit.hpp\"\n";
   pp.out << "#include <cstddef>\n";
   pp.out << "#include <functional>\n";
   pp.out << JIT_STRINGIFY(JIT_HASH_COMBINE_ARGS_SRC) << "\n";
   pp.out << "#line 1 \"" << pp.file <<"\"\n";
}

// *****************************************************************************
void ppKerDbg(context_t &pp)
{
   pp.ker.Targs += "\033[33mTargs\033[m";
   pp.ker.Tparams += "\033[33mTparams\033[m";
   pp.ker.Tformat += "\033[33mTformat\033[m";
   pp.ker.args += "\033[33margs\033[m";
   pp.ker.params += "\033[33mparams\033[m";
   pp.ker.args_wo_amp += "\033[33margs_wo_amp\033[m";
}

// *****************************************************************************
void jitArgs(context_t &pp)
{
   if (! pp.ker.__jit) { return; }
   pp.ker.mfem_cxx = JIT_STRINGIFY(MFEM_CXX);
   pp.ker.mfem_build_flags = JIT_STRINGIFY(MFEM_BUILD_FLAGS);
   pp.ker.mfem_install_dir = JIT_STRINGIFY(MFEM_INSTALL_DIR);
   pp.ker.Targs.clear();
   pp.ker.Tparams.clear();
   pp.ker.Tformat.clear();
   pp.ker.args.clear();
   pp.ker.params.clear();
   pp.ker.args_wo_amp.clear();
   pp.ker.d2u.clear();
   pp.ker.u2d.clear();
   const bool single_source = pp.ker.__single_source;
   //ppKerDbg(pp);
   //DBG("%s",single_source?"single_source":"");
   for (argument_it ia = pp.args.begin(); ia != pp.args.end() ; ia++)
   {
      const argument_t &arg = *ia;
      const bool is_const = arg.is_const;
      const bool is_amp = arg.is_amp;
      const bool is_ptr = arg.is_ptr;
      const bool is_pointer = is_ptr or is_amp;
      const char *type = arg.type.c_str();
      const char *name = arg.name.c_str();
      const bool has_default_value = arg.has_default_value;
      //DBG("\narg: %s %s %s%s", is_const?"const":"", type, is_pointer?"*|& ":"",name)
      // const and not is_pointer => add it to the template args
      if (is_const and not is_pointer and (has_default_value or not single_source))
      {
         //DBG(" => 1")
         const bool is_double = strcmp(type,"double")==0;
         // Tformat
         if (! pp.ker.Tformat.empty()) { pp.ker.Tformat += ","; }
         if (! has_default_value)
         {
            pp.ker.Tformat += is_double?"0x%lx":"%ld";
         }
         else
         {
            pp.ker.Tformat += "%ld";
         }
         // Targs
         if (! pp.ker.Targs.empty()) { pp.ker.Targs += ","; }
         pp.ker.Targs += is_double?"u":"";
         pp.ker.Targs += is_pointer?"_":"";
         pp.ker.Targs += name;
         // Tparams
         if (!has_default_value)
         {
            if (! pp.ker.Tparams.empty()) { pp.ker.Tparams += ","; }
            pp.ker.Tparams += "const ";
            pp.ker.Tparams += is_double?"uint64_t":type;
            pp.ker.Tparams += " ";
            pp.ker.Tparams += is_double?"t":"";
            pp.ker.Tparams += is_pointer?"_":"";
            pp.ker.Tparams += name;
         }
         if (is_double)
         {
            {
               pp.ker.d2u += "\n\tconst union_du union_";
               pp.ker.d2u += name;
               pp.ker.d2u += " = (union_du){u:t";
               pp.ker.d2u += is_pointer?"_":"";
               pp.ker.d2u += name;
               pp.ker.d2u += "};";

               pp.ker.d2u += "\n\tconst double ";
               pp.ker.d2u += is_pointer?"_":"";
               pp.ker.d2u += name;
               pp.ker.d2u += " = union_";
               pp.ker.d2u += name;
               pp.ker.d2u += ".d;";
            }
            {
               pp.ker.u2d += "\n\tconst uint64_t u";
               pp.ker.u2d += is_pointer?"_":"";
               pp.ker.u2d += name;
               pp.ker.u2d += " = (union_du){";
               pp.ker.u2d += is_pointer?"_":"";
               pp.ker.u2d += name;
               pp.ker.u2d += "}.u;";
            }
         }
      }

      //
      if (is_const and not is_pointer and not has_default_value and single_source)
      {
         //DBG(" => 2")
         if (! pp.ker.args.empty())
         {
            pp.ker.args += ",";
         }
         pp.ker.args += name;
         if (! pp.ker.args_wo_amp.empty())
         {
            pp.ker.args_wo_amp += ",";
         }
         pp.ker.args_wo_amp += name;

         if (! pp.ker.params.empty())
         {
            pp.ker.params += ",";
         }
         pp.ker.params += "const ";
         pp.ker.params += type;
         pp.ker.params += " ";
         pp.ker.params += name;
      }

      // !const && !pointer => std args
      if (not is_const and not is_pointer)
      {
         //DBG(" => 3")
         if (! pp.ker.args.empty())
         {
            pp.ker.args += ",";
         }
         pp.ker.args += name;
         if (! pp.ker.args_wo_amp.empty())
         {
            pp.ker.args_wo_amp += ",";
         }
         pp.ker.args_wo_amp += name;

         if (! pp.ker.params.empty())
         {
            pp.ker.params += ",";
         }
         pp.ker.params += type;
         pp.ker.params += " ";
         pp.ker.params += name;
      }
      //
      if (is_const and not is_pointer and has_default_value)
      {
         //DBG(" => 4")
         // other_parameters
         if (! pp.ker.params.empty())
         {
            pp.ker.params += ",";
         }
         pp.ker.params += " const ";
         pp.ker.params += type;
         pp.ker.params += " ";
         pp.ker.params += name;
         // other_arguments_wo_amp
         if (! pp.ker.args_wo_amp.empty())
         {
            pp.ker.args_wo_amp += ",";
         }
         pp.ker.args_wo_amp += "0";
         // other_arguments
         if (! pp.ker.args.empty())
         {
            pp.ker.args += ",";
         }
         pp.ker.args += "0";
      }

      // pointer
      if (is_pointer)
      {
         //DBG(" => 5")
         // other_arguments
         if (! pp.ker.args.empty())
         {
            pp.ker.args += ",";
         }
         pp.ker.args += is_amp?"&":"";
         pp.ker.args += is_pointer?"_":"";
         pp.ker.args += name;
         // other_arguments_wo_amp
         if (! pp.ker.args_wo_amp.empty())
         {
            pp.ker.args_wo_amp += ",";
         }
         pp.ker.args_wo_amp += is_pointer?"_":"";
         pp.ker.args_wo_amp += name;
         // other_parameters
         if (! pp.ker.params.empty())
         {
            pp.ker.params += ",";
         }
         pp.ker.params += is_const?"const ":"";
         pp.ker.params += type;
         pp.ker.params += " *";
         pp.ker.params += is_pointer?"_":"";
         pp.ker.params += name;
      }
   }
   if (pp.ker.__single_source)
   {
      //DBG(" => 6")
      if (not pp.ker.Tparams.empty()) { pp.ker.Tparams += ","; }
      pp.ker.Tparams += pp.ker.Tparams_src;
   }
}

// *****************************************************************************
void jitPrefix(context_t &pp)
{
   if (not pp.ker.__jit) { return; }
   pp.out << "\n\tconst char *src=R\"_(";
   pp.out << "#include <cstdint>";
   pp.out << "\n#include <limits>";
   pp.out << "\n#include <cstring>";
   pp.out << "\n#include <stdbool.h>";
   pp.out << "\n#include \"mfem.hpp\"";
   pp.out << "\n#include \"mfem/general/forall.hpp\"";
   if (not pp.ker.embed.empty())
   {
      // push to suppress 'declared but never referenced' warnings
      pp.out << "\n#pragma push";
      pp.out << "\n#pragma diag_suppress 177\n";
      pp.out << pp.ker.embed.c_str();
      pp.out << "\n#pragma pop";
   }
   pp.out << "\nusing namespace mfem;\n";
   pp.out << "\ntemplate<" << pp.ker.Tparams << ">";
   pp.out << "\nvoid ker_" << pp.ker.name << "(";
   pp.out << pp.ker.params << "){";
   if (not pp.ker.d2u.empty()) { pp.out << "\n\t" << pp.ker.d2u; }
   // Starts counting the block depth
   pp.block = 0;
}

// *****************************************************************************
void jitPostfix(context_t &pp)
{
   if (not pp.ker.__jit) { return; }
   if (pp.block>=0 && pp.in.peek() == '{') { pp.block++; }
   if (pp.block>=0 && pp.in.peek() == '}') { pp.block--; }
   if (pp.block!=-1) { return; }
   pp.out << "}\nextern \"C\"\nvoid k%016lx(" << pp.ker.params << "){";
   pp.out << "ker_" << pp.ker.name
          << "<" << pp.ker.Tformat << ">"
          << "(" << pp.ker.args_wo_amp << ");";
   pp.out << "\n})_\";";
   // typedef, hash map and launch
   pp.out << "\n\ttypedef void (*kernel_t)("<<pp.ker.params<<");";
   pp.out << "\n\tstatic std::unordered_map<size_t,jit::kernel<kernel_t>*> ks;";
   if (not pp.ker.u2d.empty()) { pp.out << "\n\t" << pp.ker.u2d; }
   pp.out << "\n\tconst char *cxx = \"" << pp.ker.mfem_cxx << "\";";
   pp.out << "\n\tconst char *mfem_build_flags = \""
          << pp.ker.mfem_build_flags <<  "\";";
   pp.out << "\n\tconst char *mfem_install_dir = \""
          << pp.ker.mfem_install_dir <<  "\";";
   pp.out << "\n\tconst size_t args_seed = std::hash<size_t>()(0);";
   pp.out << "\n\tconst size_t args_hash = jit::hash_args(args_seed,"
          << pp.ker.Targs << ");";
   pp.out << "\n\tif (!ks[args_hash]){";
   pp.out << "\n\t\tks[args_hash] = new jit::kernel<kernel_t>"
          << "(cxx, src, mfem_build_flags, mfem_install_dir, "
          << pp.ker.Targs << ");";
   pp.out << "\n\t}";
   pp.out << "\n\tks[args_hash]->operator_void(" << pp.ker.args << ");\n";
   // Stop counting the blocks and flush the kernel status
   pp.block--;
   pp.ker.__jit = false;
}

// *****************************************************************************
string arg_get_array_type(context_t &pp)
{
   string type;
   skip_space(pp);
   check(pp,pp.in.peek()=='<',"no '<' while in get_array_type");
   put(pp);
   type += "<";
   skip_space(pp);
   check(pp,is_id(pp),"no type found while in get_array_type");
   string id = get_id(pp);
   pp.out << id.c_str();
   type += id;
   skip_space(pp);
   check(pp,pp.in.peek()=='>',"no '>' while in get_array_type");
   put(pp);
   type += ">";
   return type;
}

// *****************************************************************************
bool jitGetArgs(context_t &pp)
{
   bool empty = true;
   argument_t arg;
   pp.args.clear();
   // Go to first possible argument
   skip_space(pp);
   if (isvoid(pp))
   {
      drop_name(pp);
      return true;
   }
   for (int p=0; true; empty=false)
   {
      if (is_star(pp))
      {
         arg.is_ptr = true;
         put(pp);
         continue;
      }
      if (is_amp(pp))
      {
         arg.is_amp = true;
         put(pp);
         continue;
      }
      if (is_coma(pp))
      {
         put(pp);
         continue;
      }
      if (is_left_parenthesis(pp))
      {
         p+=1;
         put(pp);
         continue;
      }
      const string &id = peekid(pp);
      drop_name(pp);
      // Qualifiers
      if (id=="const") { pp.out << id; arg.is_const = true; continue; }
      if (id=="__restrict") { pp.out << id; arg.is_restrict = true; continue; }
      // Types
      if (id=="char") { pp.out << id; arg.type = id; continue; }
      if (id=="int") { pp.out << id; arg.type = id; continue; }
      if (id=="short") { pp.out << id; arg.type = id; continue; }
      if (id=="unsigned") { pp.out << id; arg.type = id; continue; }
      if (id=="long") { pp.out << id; arg.type = id; continue; }
      if (id=="bool") { pp.out << id; arg.type = id; continue; }
      if (id=="float") { pp.out << id; arg.type = id; continue; }
      if (id=="double") { pp.out << id; arg.type = id; continue; }
      if (id=="size_t") { pp.out << id; arg.type = id; continue; }
      if (id=="Array")
      {
         pp.out << id; arg.type = id;
         arg.type += arg_get_array_type(pp);
         continue;
      }
      if (id=="Vector") { pp.out << id; arg.type = id; continue; }
      if (id=="DofToQuad") { pp.out << id; arg.type = id; continue; }
      const bool is_pointer = arg.is_ptr || arg.is_amp;
      const bool underscore = is_pointer;
      pp.out << (underscore?"_":"") << id;
      // focus on the name, we should have qual & type
      arg.name = id;
      // now check for a possible default value
      next(pp);
      if (is_eq(pp))
      {
         put(pp);
         next(pp);
         arg.has_default_value = true;
         arg.default_value = get_digit(pp);
         pp.out << arg.default_value;
      }
      else
      {
         // check if id has a T_id in pp.ker.Tparams_src
         string t_id("t_");
         t_id += id;
         std::transform(t_id.begin(), t_id.end(), t_id.begin(), ::toupper);
         // if we have a hit, fake it has_default_value to trig the args to <>
         if (pp.ker.Tparams_src.find(t_id) != string::npos)
         {
            arg.has_default_value = true;
            arg.default_value = 0;
         }
      }
      pp.args.push_back(arg);
      arg = argument_t();
      int c = pp.in.peek();
      assert(not pp.in.eof());
      if (c == ')') { p-=1; if (p>=0) { put(pp); continue; } }
      if (p<0) { break; }
      check(pp,pp.in.peek()==',',"no coma while in args");
      put(pp);
   }
   // Prepare the kernel strings from the arguments
   jitArgs(pp);
   return empty;
}

// *****************************************************************************
void jitAmpFromPtr(context_t &pp)
{
   for (argument_it ia = pp.args.begin(); ia != pp.args.end() ; ia++)
   {
      const argument_t a = *ia;
      const bool is_const = a.is_const;
      const bool is_ptr = a.is_ptr;
      const bool is_amp = a.is_amp;
      const bool is_pointer = is_ptr || is_amp;
      const char *type = a.type.c_str();
      const char *name = a.name.c_str();
      const bool underscore = is_pointer;
      if (is_const && underscore)
      {
         pp.out << "\n\tconst " << type << (is_amp?"&":"*") << name
                << " = " <<  (is_amp?"*":"")
                << " _" << name << ";";
      }
      if (!is_const && underscore)
      {
         pp.out << "\n\t" << type << (is_amp?"&":"*") << name
                << " = " << (is_amp?"*":"")
                << " _" << name << ";";
      }
   }
}

// *****************************************************************************
void __jit(context_t &pp)
{
   pp.ker.__jit = true;
   next(pp);
   // return type should be void for now, or we could hit a 'static'
   // or even a 'template' which triggers the '__single_source' case
   const bool check_next_id = isvoid(pp) or isstatic(pp) or istemplate(pp);
   // first check for the template
   check(pp,  check_next_id, "kernel w/o void, static or template");
   if (istemplate(pp))
   {
      // copy the 'template<...>' in Tparams_src
      pp.out << get_id(pp);
      // tag our kernel as a '__single_source' one
      pp.ker.__single_source = true;
      next(pp);
      check(pp, pp.in.peek()=='<',"no '<' in single source kernel!");
      put(pp);
      pp.ker.Tparams_src.clear();
      while (pp.in.peek() != '>')
      {
         assert(not pp.in.eof());
         char c = get(pp);
         put(c,pp);
         pp.ker.Tparams_src += c;
      }
      put(pp);
   }
   // 'static' check
   if (isstatic(pp)) { pp.out << get_id(pp); }
   next(pp);
   const string void_return_type = get_id(pp);
   pp.out << void_return_type;
   // Get kernel's name or namespace
   pp.ker.name.clear();
   pp.ker.space.clear();
   next(pp);
   const string name = get_id(pp);
   pp.out << name;
   pp.ker.name = name;
   if  (isnamespace(pp))
   {
      check(pp,pp.in.peek()==':',"no 1st ':' in namespaced kernel");
      put(pp);
      check(pp,pp.in.peek()==':',"no 2st ':' in namespaced kernel");
      put(pp);
      const string real_name = get_id(pp);
      pp.out << real_name;
      pp.ker.name = real_name;
      pp.ker.space = name;
   }
   next(pp);
   // check we are at the left parenthesis
   check(pp,pp.in.peek()=='(',"no 1st '(' in kernel");
   put(pp);
   // Get the arguments
   jitGetArgs(pp);
   // Make sure we have hit the last ')' of the arguments
   check(pp,pp.in.peek()==')',"no last ')' in kernel");
   put(pp);
   next(pp);
   // Make sure we are about to start a compound statement
   check(pp,pp.in.peek()=='{',"no compound statement found");
   put(pp);
   // Generate the kernel prefix for this kernel
   jitPrefix(pp);
   // Generate the & <=> * transformations
   jitAmpFromPtr(pp);
}

// *****************************************************************************
// * MFEM_EMBED
// *****************************************************************************
void __embed(context_t &pp)
{
   pp.ker.__embed = true;
   // Goto first '{'
   while ('{' != put(pp));
   // Starts counting the compound statements
   pp.block = 0;
}

// *****************************************************************************
void embedPostfix(context_t &pp)
{
   if (not pp.ker.__embed) { return; }
   if (pp.block>=0 && pp.in.peek() == '{') { pp.block++; }
   if (pp.block>=0 && pp.in.peek() == '}') { pp.block--; }
   if (pp.block!=-1) { return; }
   check(pp,pp.in.peek()=='}',"no compound statements found");
   put(pp);
   pp.block--;
   pp.ker.__embed = false;
   pp.ker.embed += "\n";
}

// *****************************************************************************
// * MFEM_TEMPLATE and MFEM_RANGE
// *****************************************************************************
void __range(context_t &pp, argument_t &arg)
{
   char c;
   bool dash = false;
   // Verify and eat '('
   check(pp,get(pp)=='(',"templated kernel should declare the range");
   do
   {
      const int n = get_digit(pp);
      if (dash)
      {
         for (int i=arg.range.back()+1; i<n; i++)
         {
            arg.range.push_back(i);
         }
      }
      dash = false;
      arg.range.push_back(n);
      c = get(pp);
      assert(not pp.in.eof());
      check(pp, c==',' or c=='-' or  c==')', "unknown MFEM_TEMPLATE range");
      if (c=='-')
      {
         dash = true;
      }
   }
   while (c!=')');
}

// *****************************************************************************
void templateGetArgs(context_t &pp)
{
   int nargs = 0;
   int targs = 0;
   argument_t arg;
   pp.args.clear();
   // Go to first possible argument
   drop_space(pp);
   if (isvoid(pp)) { assert(false); }
   string current_arg;
   for (int p=0; true;)
   {
      skip_space(pp,current_arg);
      comments(pp);
      if (is_star(pp))
      {
         arg.is_ptr = true;
         current_arg += get(pp);
         continue;
      }
      skip_space(pp,current_arg);
      comments(pp);
      if (is_coma(pp))
      {
         current_arg += get(pp);
         continue;
      }
      const string &id = peekid(pp);
      drop_name(pp);
      // Qualifiers
      if (id=="MFEM_RANGE") { __range(pp,arg); arg.is_tpl = true; continue; }
      if (id=="const") { current_arg += id; arg.is_const = true; continue; }
      // Types
      if (id=="char") { current_arg += id; arg.type = id; continue; }
      if (id=="int") { current_arg += id; arg.type = id; continue; }
      if (id=="short") { current_arg += id; arg.type = id; continue; }
      if (id=="unsigned") { current_arg += id; arg.type = id; continue; }
      if (id=="long") { current_arg += id; arg.type = id; continue; }
      if (id=="bool") { current_arg += id; arg.type = id; continue; }
      if (id=="float") { current_arg += id; arg.type = id; continue; }
      if (id=="double") { current_arg += id; arg.type = id; continue; }
      if (id=="size_t") { current_arg += id; arg.type = id; continue; }
      // focus on the name, we should have qual & type
      arg.name = id;
      if (not arg.is_tpl)
      {
         pp.args.push_back(arg);
         pp.ker.tpl.signature += current_arg + id;
         {
            pp.ker.tpl.args += (nargs==0)?"":", ";
            pp.ker.tpl.args +=  arg.name;
         }
         nargs += 1;
      }
      else
      {
         pp.ker.tpl.Tparams += (targs==0)?"":", ";
         pp.ker.tpl.Tparams += "const " + arg.type + " " + arg.name;
         pp.ker.tpl.ranges.push_back(arg.range);
         {
            pp.ker.tpl.Targs += (targs==0)?"":", ";
            pp.ker.tpl.Targs += arg.name;
         }
         targs += 1;
      }
      pp.ker.tpl.params += current_arg + id + (nargs==0 and targs>0?",":"");
      arg = argument_t();
      current_arg = string();
      const int c = pp.in.peek();
      assert(not pp.in.eof());
      if (c == '(') { p+=1; }
      if (c == ')') { p-=1; }
      if (p<0) { break; }
      skip_space(pp,current_arg);
      comments(pp);
      check(pp,pp.in.peek()==',',"no coma while in args");
      get(pp);
      if (nargs>0) { current_arg += ","; }
   }
}

// *****************************************************************************
void __template(context_t &pp)
{
   pp.ker.__template = true;
   pp.ker.tpl = template_t();
   drop_space(pp);
   comments(pp);
   check(pp, isvoid(pp) or isstatic(pp),"template w/o void or static");
   if (isstatic(pp))
   {
      pp.ker.tpl.return_t += get_id(pp);
      skip_space(pp,pp.ker.tpl.return_t);
   }
   const string void_return_type = get_id(pp);
   pp.ker.tpl.return_t += void_return_type;
   // Get kernel's name
   skip_space(pp,pp.ker.tpl.return_t);
   const string name = get_id(pp);
   pp.ker.name = name;
   skip_space(pp, pp.ker.tpl.return_t);
   // check we are at the left parenthesis
   check(pp,pp.in.peek()=='(',"no 1st '(' in kernel");
   get(pp);
   // Get the arguments
   templateGetArgs(pp);
   // Make sure we have hit the last ')' of the arguments
   check(pp,pp.in.peek()==')',"no last ')' in kernel");
   pp.ker.tpl.signature += get(pp);
   // Now dump the templated kernel needs before the body
   pp.out << "template<";
   pp.out << pp.ker.tpl.Tparams;
   pp.out << ">\n";
   pp.out << pp.ker.tpl.return_t;
   pp.out << "__" << pp.ker.name;
   pp.out << "(" << pp.ker.tpl.signature;
   // Std body dump to pp.out
   skip_space(pp);
   // Make sure we are about to start a compound statement
   check(pp,pp.in.peek()=='{',"no compound statement found");
   put(pp);
   // Starts counting the compound statements
   pp.block = 0;
}

// *****************************************************************************
static list<list<int> > templateOuterProduct(const list<list<int> > &v)
{
   list<list<int> > s = {{}};
   for (const auto &u : v)
   {
      list<list<int> > r;
      for (const auto &x:s)
      {
         for (const auto y:u)
         {
            r.push_back(x);
            r.back().push_back(y);
         }
      }
      s = std::move(r);
   }
   return s;
}

// *****************************************************************************
void templatePostfix(context_t &pp)
{
   if (not pp.ker.__template) { return; }
   if (pp.block>=0 && pp.in.peek() == '{') { pp.block++; }
   if (pp.block>=0 && pp.in.peek() == '}') { pp.block--; }
   if (pp.block!=-1) { return; }
   check(pp,pp.in.peek()=='}',"no compound statements found");
   put(pp);
   // Stop counting the compound statements and flush the T status
   pp.block--;
   pp.ker.__template = false;
   // Now push template kernel launcher
   pp.out << "\n" << pp.ker.tpl.return_t << pp.ker.name;
   pp.out << "(" << pp.ker.tpl.params << "){";
   pp.out << "\n\ttypedef ";
   pp.out << pp.ker.tpl.return_t << "(*__T" << pp.ker.name << ")";
   pp.out << "(" << pp.ker.tpl.signature << ";";
   pp.out << "\n\tconst size_t id = hash_args(std::hash<size_t>()(0), "
          << pp.ker.tpl.Targs << ");";
   pp.out << "\n\tstatic std::unordered_map<size_t, "
          << "__T" << pp.ker.name << "> call = {";
   for (list<int> range : templateOuterProduct(pp.ker.tpl.ranges))
   {
      pp.out << "\n\t\t{";
      size_t i=1;
      const size_t n = range.size();
      size_t hash = 0;
      for (int r : range) { hash = hash_args(hash,r); }
      pp.out << std::hex << "0x" << hash;
      pp.out << ",&__"<<pp.ker.name<<"<";
      for (int r : range)
      {
         pp.out << to_string(r) << (i==n?"":",");
         i+=1;
      }
      pp.out << ">},";
   }
   pp.out << "\n\t};";
   pp.out << "\n\tassert(call[id]);";
   pp.out << "\n\tcall[id](";
   pp.out << pp.ker.tpl.args;
   pp.out << ");";
   pp.out << "\n}";
}


// *****************************************************************************
// * MFEM_UNROLL
// *****************************************************************************
void __unroll(context_t &pp)
{
   //DBG("__unroll")
   while ('(' != get(pp)) {assert(not pp.in.eof());}
   drop(pp);
   string depth = get_id(pp);
   //DBG("(%s)",depth.c_str());
   drop(pp);
   check(pp,is_right_parenthesis(pp),"no last right parenthesis found");
   get(pp);
   drop(pp);
   check(pp,is_semicolon(pp),"no last semicolon found");
   get(pp);
   // only if we are in a forall, we push the unrolling
   if (pp.ker.__forall)
   {
      pp.ker.forall.body += "#pragma unroll ";
      pp.ker.forall.body += depth.c_str();
   }
}

// *****************************************************************************
// * MFEM_FORALL_2D
// *****************************************************************************
void __forall2D(context_t &pp)
{
   //DBG("__forall2D")
   pp.ker.__forall = true;
   pp.ker.forall.body.clear();

   check(pp,is_left_parenthesis(pp),"no 1st '(' in forall 2D");
   get(pp); // drop '('
   pp.ker.forall.e = get_id(pp);
   //DBG("iterator:'%s'", pp.ker.forall.e.c_str());

   check(pp,is_coma(pp),"no 1st coma in forall 2D");
   get(pp); // drop ','

   drop(pp);
   check(pp,is_id(pp),"no 1st id(N) in forall 2D");
   pp.ker.forall.N = get_id(pp);
   //DBG("N:'%s'", pp.ker.forall.N.c_str());
   drop(pp);
   check(pp,is_coma(pp),"no 2nd coma in forall 2D");
   get(pp); // drop ','

   drop(pp);
   check(pp,is_id(pp),"no 2st id (X) in forall 2D");
   pp.ker.forall.X = get_id(pp);
   //DBG("X:'%s'", pp.ker.forall.X.c_str());
   drop(pp);
   //DBG(">%c<", put(pp));
   check(pp,is_coma(pp),"no 3rd coma in forall 2D");
   get(pp); // drop ','

   drop(pp);
   check(pp,is_id(pp),"no 3rd id (Y) in forall 2D");
   pp.ker.forall.Y = get_id(pp);
   //DBG("Y:'%s'", pp.ker.forall.Y.c_str());
   drop(pp);
   check(pp,is_coma(pp),"no 4th coma in forall 2D");
   get(pp); // drop ','

   drop(pp);
   check(pp,is_id(pp),"no 4th id (Y) in forall 2D");
   pp.ker.forall.Z = get_id(pp);
   //DBG("Z:'%s'", pp.ker.forall.Z.c_str());
   drop(pp);
   check(pp,is_coma(pp),"no last coma in forall 2D");
   get(pp); // drop ','

   // Starts counting the parentheses
   pp.parenthesis = 0;
}
// *****************************************************************************
void forall2DPostfix(context_t &pp)
{
   if (not pp.ker.__forall) { return; }
   //DBG("forall2DPostfix 1")
   if (pp.parenthesis>=0 && pp.in.peek() == '(') { pp.parenthesis++; }
   if (pp.parenthesis>=0 && pp.in.peek() == ')') { pp.parenthesis--; }
   if (pp.parenthesis!=-1) { return; }
   //DBG("forall2DPostfix 2")
   drop(pp);
   check(pp,is_right_parenthesis(pp),"no last right parenthesis found");
   get(pp);
   drop(pp);
   check(pp,is_semicolon(pp),"no last semicolon found");
   get(pp);
   pp.parenthesis--;
   pp.ker.__forall = false;
   //DBG("%s",pp.ker.forall.body.c_str());
   pp.out << "\nForallWrap<2>(true, " << pp.ker.forall.N.c_str() << ",";
   pp.out << "\n[=] MFEM_DEVICE (int " << pp.ker.forall.e <<")";
   pp.out << pp.ker.forall.body.c_str() << ",";
   pp.out << "\n[&] (int " << pp.ker.forall.e <<")";
   pp.out << pp.ker.forall.body.c_str() << ",";
   pp.out << "\n" ;
   pp.out << pp.ker.forall.X.c_str() << ",";
   pp.out << pp.ker.forall.Y.c_str() << ",";
   pp.out << pp.ker.forall.Z.c_str() << ");";
}

// *****************************************************************************
// * MFEM preprocessor
// *****************************************************************************
void tokens(context_t &pp)
{
   if (peekn(pp,4) != "MFEM") { return; }
   const string id = get_id(pp);
   //DBG(id.c_str());
   if (id == "MFEM_JIT") { return __jit(pp); }
   if (id == "MFEM_EMBED") { return __embed(pp); }
   if (id == "MFEM_UNROLL") { return __unroll(pp); }
   if (id == "MFEM_TEMPLATE") { return __template(pp); }
   if (id == "MFEM_FORALL_2D") { return __forall2D(pp); }
   if (pp.ker.__embed ) { pp.ker.embed += id; }
   if (pp.ker.__forall) { pp.ker.forall.body += id; return;}
   pp.out << id;
}

// *****************************************************************************
inline bool eof(context_t &pp)
{
   const char c = get(pp);
   if (pp.in.eof()) { return true; }
   put(c,pp);
   return false;
}

// *****************************************************************************
int preprocess(context_t &pp)
{
   jitHeader(pp);
   pp.ker.__jit = false;
   pp.ker.__embed = false;
   pp.ker.__forall = false;
   pp.ker.__template = false;
   pp.ker.__single_source = false;
   do
   {
      tokens(pp);
      comments(pp);
      jitPostfix(pp);
      embedPostfix(pp);
      forall2DPostfix(pp);
      templatePostfix(pp);
   }
   while (not eof(pp));
   return 0;
}

} // namespace mfem

// *****************************************************************************
int main(const int argc, char* argv[])
{
   string input, output, file;
   if (argc<=1) { return mfem::help(argv); }
   for (int i=1; i<argc; i+=1)
   {
      // -h lauches help
      if (argv[i] == string("-h")) { return mfem::help(argv); }
      // -o fills output
      if (argv[i] == string("-o"))
      {
         output = argv[i+=1];
         continue;
      }
      // should give input file
      const char* last_dot = mfem::strrnc(argv[i],'.');
      const size_t ext_size = last_dot?strlen(last_dot):0;
      if (last_dot && ext_size>0)
      {
         assert(file.size()==0);
         file = input = argv[i];
      }
   }
   assert(!input.empty());
   const bool output_file = !output.empty();
   ifstream in(input.c_str(), ios::in | ios::binary);
   ofstream out(output.c_str(), ios::out | ios::binary | ios::trunc);
   assert(!in.fail());
   assert(in.is_open());
   if (output_file) {assert(out.is_open());}
   mfem::context_t pp(in,output_file?out:cout,file);
   try { mfem::preprocess(pp); }
   catch (mfem::error_t err)
   {
      cerr << endl << err.file << ":" << err.line << ":"
           << " mpp error" << (err.msg?": ":"") << (err.msg?err.msg:"") << endl;
      remove(output.c_str());
      return ~0;
   }
   in.close();
   out.close();
   return 0;
}
