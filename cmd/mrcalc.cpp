/*
 * Copyright (c) 2008-2016 the MRtrix3 contributors
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/
 * 
 * MRtrix is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * For more details, see www.mrtrix.org
 * 
 */


#include "command.h"
#include "image.h"
#include "memory.h"
#include "math/rng.h"
#include "algo/threaded_copy.h"


using namespace MR;
using namespace App;


void usage () {

AUTHOR = "J-Donald Tournier (jdtournier@gmail.com)";

DESCRIPTION
  + "apply generic voxel-wise mathematical operations to images."

  + "This command will only compute per-voxel operations. "
  "Use 'mrmath' to compute summary statistics across images or "
  "along image axes."
  
  + "This command uses a stack-based syntax, with operators "
  "(specified using options) operating on the top-most entries "
  "(i.e. images or values) in the stack. Operands (values or "
  "images) are pushed onto the stack in the order they appear "
  "(as arguments) on the command-line, and operators (specified "
  "as options) operate on and consume the top-most entries in "
  "the stack, and push their output as a new entry on the stack. "
  "For example:"
  
  + "    $ mrcalc a.mif 2 -mult r.mif"
  
  + "performs the operation r = 2*a for every voxel a,r in "
  "images a.mif and r.mif respectively. Similarly:"
  
  + "    $ mrcalc a.mif -neg b.mif -div -exp 9.3 -mult r.mif"
  
  + "performs the operation r = 9.3*exp(-a/b), and:"
  
  + "    $ mrcalc a.mif b.mif -add c.mif d.mif -mult 4.2 -add -div r.mif"
  
  + "performs r = (a+b)/(c*d+4.2)."
  
  + "As an additional feature, this command will allow images with different "
  "dimensions to be processed, provided they satisfy the following "
  "conditions: for each axis, the dimensions match if they are the same size, "
  "or one of them has size one. In the latter case, the entire image will be "
  "replicated along that axis. This allows for example a 4D image of "
  "size [ X Y Z N ] to be added to a 3D image of size [ X Y Z ], as if it "
  "consisted of N copies of the 3D image along the 4th axis (the missing "
  "dimension is assumed to have size 1). Another example would a "
  "single-voxel 4D image of size [ 1 1 1 N ], multiplied by a 3D image of "
  "size [ X Y Z ], which would allow the creation of a 4D image where each "
  "volume consists of the 3D image scaled by the corresponding value for "
  "that volume in the single-voxel image.";

ARGUMENTS
  + Argument ("operand", "an input image, intensity value, or the special keywords "
      "'rand' (random number between 0 and 1) or 'randn' (random number from unit "
      "std.dev. normal distribution).").type_text().allow_multiple();

OPTIONS
  + OptionGroup ("Unary operators")

  + Option ("abs", "absolute value").allow_multiple()
  + Option ("neg", "negative value").allow_multiple()
  + Option ("sqrt", "square root").allow_multiple()
  + Option ("exp", "exponential function").allow_multiple()
  + Option ("log", "natural logarithm").allow_multiple()
  + Option ("log10", "common logarithm").allow_multiple()
  + Option ("cos", "cosine").allow_multiple()
  + Option ("sin", "sine").allow_multiple()
  + Option ("tan", "tangent").allow_multiple()
  + Option ("cosh", "hyperbolic cosine").allow_multiple()
  + Option ("sinh", "hyperbolic sine").allow_multiple()
  + Option ("tanh", "hyperbolic tangent").allow_multiple()
  + Option ("acos", "inverse cosine").allow_multiple()
  + Option ("asin", "inverse sine").allow_multiple()
  + Option ("atan", "inverse tangent").allow_multiple()
  + Option ("acosh", "inverse hyperbolic cosine").allow_multiple()
  + Option ("asinh", "inverse hyperbolic sine").allow_multiple()
  + Option ("atanh", "inverse hyperbolic tangent").allow_multiple()
  + Option ("round", "round to nearest integer").allow_multiple()
  + Option ("ceil", "round up to nearest integer").allow_multiple()
  + Option ("floor", "round down to nearest integer").allow_multiple()
  + Option ("isnan", "true (1) is operand is not-a-number (NaN)").allow_multiple()
  + Option ("isinf", "true (1) is operand is infinite (Inf)").allow_multiple()
  + Option ("finite", "true (1) is operand is finite (i.e. not NaN or Inf)").allow_multiple()

  + Option ("real", "real part of complex number").allow_multiple()
  + Option ("imag", "imaginary part of complex number").allow_multiple()
  + Option ("phase", "phase of complex number").allow_multiple()
  + Option ("conj", "complex conjugate").allow_multiple()

  + OptionGroup ("Binary operators")

  + Option ("add", "add values").allow_multiple()
  + Option ("subtract", "subtract nth operand from (n-1)th").allow_multiple()
  + Option ("multiply", "multiply values").allow_multiple()
  + Option ("divide", "divide (n-1)th operand by nth").allow_multiple()
  + Option ("pow", "raise (n-1)th operand to nth power").allow_multiple()
  + Option ("min", "smallest of last two operands").allow_multiple()
  + Option ("max", "greatest of last two operands").allow_multiple()
  + Option ("lt", "less-than operator (true=1, false=0)").allow_multiple()
  + Option ("gt", "greater-than operator (true=1, false=0)").allow_multiple()
  + Option ("le", "less-than-or-equal-to operator (true=1, false=0)").allow_multiple()
  + Option ("ge", "greater-than-or-equal-to operator (true=1, false=0)").allow_multiple()
  + Option ("eq", "equal-to operator (true=1, false=0)").allow_multiple()
  + Option ("neq", "not-equal-to operator (true=1, false=0)").allow_multiple()

  + Option ("complex", "create complex number using the last two operands as real,imaginary components").allow_multiple()

  + OptionGroup ("Ternary operators")

  + Option ("if", "if first operand is true (non-zero), return second operand, otherwise return third operand").allow_multiple()


  + DataType::options();
}


using real_type = float;
using complex_type = cfloat;


/**********************************************************************
  STACK FRAMEWORK:
 **********************************************************************/


class Evaluator;


class Chunk : public std::vector<complex_type> {
  public:
    complex_type value;
};


class ThreadLocalStorageItem {
  public:
    Chunk chunk;
    copy_ptr<Image<complex_type>> image;
};

class ThreadLocalStorage : public std::vector<ThreadLocalStorageItem> {
  public:

      void load (Chunk& chunk, Image<complex_type>& image) {
        for (size_t n = 0; n < image.ndim(); ++n)
          if (image.size(n) > 1)
            image.index(n) = iter->index(n);

        size_t n = 0;
        for (size_t y = 0; y < size[1]; ++y) {
          if (axes[1] < image.ndim()) if (image.size (axes[1]) > 1) image.index(axes[1]) = y;
          for (size_t x = 0; x < size[0]; ++x) {
            if (axes[0] < image.ndim()) if (image.size (axes[0]) > 1) image.index(axes[0]) = x;
            chunk[n++] = image.value();
          }
        }
      }

    Chunk& next () {
      ThreadLocalStorageItem& item ((*this)[current++]);
      if (item.image) load (item.chunk, *item.image);
      return item.chunk;
    }

    void reset (const Iterator& current_position) { current = 0; iter = &current_position; }

    const Iterator* iter;
    std::vector<size_t> axes, size;

  private:
    size_t current;
};




class LoadedImage
{
  public:
    LoadedImage (std::shared_ptr<Image<complex_type>>& i, const bool c) :
        image (i),
        image_is_complex (c) { }
    std::shared_ptr<Image<complex_type>> image;
    bool image_is_complex;
};




class StackEntry {
  public:

    StackEntry (const char* entry) : 
      arg (entry) { }

    StackEntry (Evaluator* evaluator_p) : 
      arg (nullptr),
      evaluator (evaluator_p) { }

    void load () {
      if (!arg) 
        return;
      auto search = image_list.find (arg);
      if (search != image_list.end()) {
        DEBUG (std::string ("image \"") + arg + "\" already loaded - re-using exising image");
        image = search->second.image;
        image_is_complex = search->second.image_is_complex;
      }
      else {
        try {
          auto header = Header::open (arg);
          image_is_complex = header.datatype().is_complex();
          image.reset (new Image<complex_type> (header.get_image<complex_type>()));
          image_list.insert (std::make_pair (arg, LoadedImage (image, image_is_complex)));
        }
        catch (Exception) {
          std::string a = lowercase (arg);
          if      (a ==  "nan")  { value =  std::numeric_limits<real_type>::quiet_NaN(); }
          else if (a == "-nan")  { value = -std::numeric_limits<real_type>::quiet_NaN(); }
          else if (a ==  "inf")  { value =  std::numeric_limits<real_type>::infinity(); }
          else if (a == "-inf")  { value = -std::numeric_limits<real_type>::infinity(); }
          else if (a == "rand")  { value = 0.0; rng.reset (new Math::RNG()); rng_gausssian = false; } 
          else if (a == "randn") { value = 0.0; rng.reset (new Math::RNG()); rng_gausssian = true; } 
          else                   { value =  to<complex_type> (arg); }
        }
      }
      arg = nullptr;
    }

    const char* arg;
    std::shared_ptr<Evaluator> evaluator;
    std::shared_ptr<Image<complex_type>> image;
    copy_ptr<Math::RNG> rng;
    complex_type value;
    bool rng_gausssian;
    bool image_is_complex;

    bool is_complex () const;

    static std::map<std::string, LoadedImage> image_list;

    Chunk& evaluate (ThreadLocalStorage& storage) const;
};

std::map<std::string, LoadedImage> StackEntry::image_list;


class Evaluator
{
  public: 
    Evaluator (const std::string& name, const char* format_string, bool complex_maps_to_real = false, bool real_maps_to_complex = false) : 
      id (name),
      format (format_string),
      ZtoR (complex_maps_to_real),
      RtoZ (real_maps_to_complex) { }
    virtual ~Evaluator() { }
    const std::string id;
    const char* format;
    bool ZtoR, RtoZ;
    std::vector<StackEntry> operands;

    Chunk& evaluate (ThreadLocalStorage& storage) const {
      Chunk& in1 (operands[0].evaluate (storage));
      if (num_args() == 1) return evaluate (in1);
      Chunk& in2 (operands[1].evaluate (storage));
      if (num_args() == 2) return evaluate (in1, in2);
      Chunk& in3 (operands[2].evaluate (storage));
      return evaluate (in1, in2, in3);
    }
    virtual Chunk& evaluate (Chunk& in) const { throw Exception ("operation \"" + id + "\" not supported!"); return in; }
    virtual Chunk& evaluate (Chunk& a, Chunk& b) const { throw Exception ("operation \"" + id + "\" not supported!"); return a; }
    virtual Chunk& evaluate (Chunk& a, Chunk& b, Chunk& c) const { throw Exception ("operation \"" + id + "\" not supported!"); return a; }

    virtual bool is_complex () const {
      for (size_t n = 0; n < operands.size(); ++n) 
        if (operands[n].is_complex())  
          return !ZtoR;
      return RtoZ;
    }
    size_t num_args () const { return operands.size(); }

};



inline bool StackEntry::is_complex () const {
  if (image) return image_is_complex;
  if (evaluator) return evaluator->is_complex();
  if (rng) return false;
  return value.imag() != 0.0;
}



inline Chunk& StackEntry::evaluate (ThreadLocalStorage& storage) const
{
  if (evaluator) return evaluator->evaluate (storage);
  if (rng) {
    Chunk& chunk = storage.next();
    if (rng_gausssian) {
      std::normal_distribution<real_type> dis (0.0, 1.0);
      for (size_t n = 0; n < chunk.size(); ++n)
        chunk[n] = dis (*rng);
    }
    else {
      std::uniform_real_distribution<real_type> dis (0.0, 1.0);
      for (size_t n = 0; n < chunk.size(); ++n)
        chunk[n] = dis (*rng);
    }
    return chunk;
  }
  return storage.next();
}







inline void replace (std::string& orig, size_t n, const std::string& value)
{
  if (orig[0] == '(' && orig[orig.size()-1] == ')') {
    size_t pos = orig.find ("(%"+str(n+1)+")");
    if (pos != orig.npos) {
      orig.replace (pos, 4, value);
      return;
    }
  }

  size_t pos = orig.find ("%"+str(n+1));
  if (pos != orig.npos)
    orig.replace (pos, 2, value);
}


// TODO: move this into StackEntry class and compute string at construction
// to make sure full operation is recorded, even for scalar operations that
// get evaluated there and then and so get left out if the string is created
// later:
std::string operation_string (const StackEntry& entry) 
{
  if (entry.image)
    return entry.image->name();
  else if (entry.rng)
    return entry.rng_gausssian ? "randn()" : "rand()";
  else if (entry.evaluator) {
    std::string s = entry.evaluator->format;
    for (size_t n = 0; n < entry.evaluator->operands.size(); ++n) 
      replace (s, n, operation_string (entry.evaluator->operands[n]));
    return s;
  }
  else return str(entry.value);
}





template <class Operation>
class UnaryEvaluator : public Evaluator 
{
  public:
    UnaryEvaluator (const std::string& name, Operation operation, const StackEntry& operand) : 
      Evaluator (name, operation.format, operation.ZtoR, operation.RtoZ), 
      op (operation) { 
        operands.push_back (operand);
      }

    Operation op;

    virtual Chunk& evaluate (Chunk& in) const { 
      if (operands[0].is_complex()) 
        for (size_t n = 0; n < in.size(); ++n)
          in[n] = op.Z (in[n]);
      else 
        for (size_t n = 0; n < in.size(); ++n)
          in[n] = op.R (in[n].real());

      return in; 
    }
};






template <class Operation>
class BinaryEvaluator : public Evaluator 
{
  public:
    BinaryEvaluator (const std::string& name, Operation operation, const StackEntry& operand1, const StackEntry& operand2) : 
      Evaluator (name, operation.format, operation.ZtoR, operation.RtoZ),
      op (operation) { 
        operands.push_back (operand1);
        operands.push_back (operand2);
      }

    Operation op;

    virtual Chunk& evaluate (Chunk& a, Chunk& b) const {
      Chunk& out (a.size() ? a : b);
      if (operands[0].is_complex() || operands[1].is_complex()) {
        for (size_t n = 0; n < out.size(); ++n) 
          out[n] = op.Z (
              a.size() ? a[n] : a.value, 
              b.size() ? b[n] : b.value );
      }
      else {
        for (size_t n = 0; n < out.size(); ++n) 
          out[n] = op.R (
              a.size() ? a[n].real() : a.value.real(), 
              b.size() ? b[n].real() : b.value.real() );
      }
      return out;
    }

};



template <class Operation>
class TernaryEvaluator : public Evaluator 
{
  public:
    TernaryEvaluator (const std::string& name, Operation operation, const StackEntry& operand1, const StackEntry& operand2, const StackEntry& operand3) : 
      Evaluator (name, operation.format, operation.ZtoR, operation.RtoZ),
      op (operation) { 
        operands.push_back (operand1);
        operands.push_back (operand2);
        operands.push_back (operand3);
      }

    Operation op;

    virtual Chunk& evaluate (Chunk& a, Chunk& b, Chunk& c) const {
      Chunk& out (a.size() ? a : (b.size() ? b : c));
      if (operands[0].is_complex() || operands[1].is_complex() || operands[2].is_complex()) {
        for (size_t n = 0; n < out.size(); ++n) 
          out[n] = op.Z (
              a.size() ? a[n] : a.value,
              b.size() ? b[n] : b.value,
              c.size() ? c[n] : c.value );
      }
      else {
        for (size_t n = 0; n < out.size(); ++n) 
          out[n] = op.R (
              a.size() ? a[n].real() : a.value.real(), 
              b.size() ? b[n].real() : b.value.real(), 
              c.size() ? c[n].real() : c.value.real() );
      }
      return out;
    }

};





template <class Operation>
void unary_operation (const std::string& operation_name, std::vector<StackEntry>& stack, Operation operation)
{
  if (stack.empty()) 
    throw Exception ("no operand in stack for operation \"" + operation_name + "\"!");
  StackEntry& a (stack[stack.size()-1]);
  a.load();
  if (a.evaluator || a.image || a.rng) {
    StackEntry entry (new UnaryEvaluator<Operation> (operation_name, operation, a));
    stack.back() = entry;
  }
  else {
    try {
      a.value = ( a.value.imag() == 0.0 ? operation.R (a.value.real()) : operation.Z (a.value) );
    }
    catch (...) {
      throw Exception ("operation \"" + operation_name + "\" not supported for data type supplied");
    }
  }
}





template <class Operation>
void binary_operation (const std::string& operation_name, std::vector<StackEntry>& stack, Operation operation)
{
  if (stack.size() < 2) 
    throw Exception ("not enough operands in stack for operation \"" + operation_name + "\"");
  StackEntry& a (stack[stack.size()-2]);
  StackEntry& b (stack[stack.size()-1]);
  a.load();
  b.load();
  if (a.evaluator || a.image || a.rng || b.evaluator || b.image || b.rng) {
    StackEntry entry (new BinaryEvaluator<Operation> (operation_name, operation, a, b));
    stack.pop_back();
    stack.back() = entry;
  }
  else {
    a.value = ( a.value.imag() == 0.0  && b.value.imag() == 0.0 ? 
      operation.R (a.value.real(), b.value.real()) :
      operation.Z (a.value, b.value) );
    stack.pop_back();
  }
}




template <class Operation>
void ternary_operation (const std::string& operation_name, std::vector<StackEntry>& stack, Operation operation)
{
  if (stack.size() < 3) 
    throw Exception ("not enough operands in stack for operation \"" + operation_name + "\"");
  StackEntry& a (stack[stack.size()-3]);
  StackEntry& b (stack[stack.size()-2]);
  StackEntry& c (stack[stack.size()-1]);
  a.load();
  b.load();
  c.load();
  if (a.evaluator || a.image || a.rng || b.evaluator || b.image || b.rng || c.evaluator || c.image || c.rng) {
    StackEntry entry (new TernaryEvaluator<Operation> (operation_name, operation, a, b, c));
    stack.pop_back();
    stack.pop_back();
    stack.back() = entry;
  }
  else {
    a.value = ( a.value.imag() == 0.0  && b.value.imag() == 0.0  && c.value.imag() == 0.0 ? 
      operation.R (a.value.real(), b.value.real(), c.value.real()) :
      operation.Z (a.value, b.value, c.value) );
    stack.pop_back();
    stack.pop_back();
  }
}





/**********************************************************************
   MULTI-THREADED RUNNING OF OPERATIONS:
 **********************************************************************/


void get_header (const StackEntry& entry, Header& header) 
{
  if (entry.evaluator) {
    for (size_t n = 0; n < entry.evaluator->operands.size(); ++n)
      get_header (entry.evaluator->operands[n], header);
    return;
  }

  if (!entry.image) 
    return;

  if (header.ndim() == 0) {
    header = *entry.image;
    return;
  }

  if (header.ndim() < entry.image->ndim()) 
    header.ndim() = entry.image->ndim();
  for (size_t n = 0; n < std::min<size_t> (header.ndim(), entry.image->ndim()); ++n) {
    if (header.size(n) > 1 && entry.image->size(n) > 1 && header.size(n) != entry.image->size(n))
      throw Exception ("dimensions of input images do not match - aborting");
    header.size(n) = std::max (header.size(n), entry.image->size(n));
    if (!std::isfinite (header.spacing(n))) 
      header.spacing(n) = entry.image->spacing(n);
  }

}






class ThreadFunctor {
  public:
    ThreadFunctor (
        const std::vector<size_t>& inner_axes,
        const StackEntry& top_of_stack, 
        Image<complex_type>& output_image) :
      top_entry (top_of_stack),
      image (output_image),
      loop (Loop (inner_axes)) {
        storage.axes = loop.axes;
        storage.size.push_back (image.size(storage.axes[0]));
        storage.size.push_back (image.size(storage.axes[1]));
        chunk_size = image.size (storage.axes[0]) * image.size (storage.axes[1]);
        allocate_storage (top_entry);
      }

    void allocate_storage (const StackEntry& entry) {
      if (entry.evaluator) {
        for (size_t n = 0; n < entry.evaluator->operands.size(); ++n)
          allocate_storage (entry.evaluator->operands[n]);
        return;
      }

      storage.push_back (ThreadLocalStorageItem());
      if (entry.image) {
        storage.back().image.reset (new Image<complex_type> (*entry.image));
        storage.back().chunk.resize (chunk_size);
        return;
      }
      else if (entry.rng) {
        storage.back().chunk.resize (chunk_size);
      }
      else storage.back().chunk.value = entry.value;
    }


    void operator() (const Iterator& iter) {
      storage.reset (iter);
      assign_pos_of (iter).to (image);

      Chunk& chunk = top_entry.evaluate (storage);

      auto value = chunk.cbegin();
      for (auto l = loop (image); l; ++l) 
        image.value() = *(value++);
    }



    const StackEntry& top_entry;
    Image<complex_type> image;
    decltype (Loop (std::vector<size_t>())) loop;
    ThreadLocalStorage storage;
    size_t chunk_size;
};





void run_operations (const std::vector<StackEntry>& stack) 
{
  Header header;
  get_header (stack[0], header);

  if (header.ndim() == 0) {
    DEBUG ("no valid images supplied - assuming calculator mode");
    if (stack.size() != 1) 
      throw Exception ("too many operands left on stack!");

    assert (!stack[0].evaluator);
    assert (!stack[0].image);

    print (str (stack[0].value) + "\n");
    return;
  }

  if (stack.size() == 1) 
    throw Exception ("output image not specified");

  if (stack.size() > 2) 
    throw Exception ("too many operands left on stack!");

  if (!stack[1].arg)
    throw Exception ("output image not specified");

  if (stack[0].is_complex()) {
    header.datatype() = DataType::from_command_line (DataType::CFloat32);
    if (!header.datatype().is_complex())
      throw Exception ("output datatype must be complex");
  }
  else header.datatype() = DataType::from_command_line (DataType::Float32);

  auto output = Header::create (stack[1].arg, header).get_image<complex_type>();

  auto loop = ThreadedLoop ("computing: " + operation_string(stack[0]), output, 0, output.ndim(), 2);

  ThreadFunctor functor (loop.inner_axes, stack[0], output);
  loop.run_outer (functor);
}







/**********************************************************************
        OPERATIONS BASIC FRAMEWORK:
**********************************************************************/

class OpBase {
  public:
    OpBase (const char* format_string, bool complex_maps_to_real = false, bool real_map_to_complex = false) :
      format (format_string),
      ZtoR (complex_maps_to_real),
      RtoZ (real_map_to_complex) { }
    const char* format;
    const bool ZtoR, RtoZ;
};

class OpUnary : public OpBase {
  public:
    OpUnary (const char* format_string, bool complex_maps_to_real = false, bool real_map_to_complex = false) :
      OpBase (format_string, complex_maps_to_real, real_map_to_complex) { }
    complex_type R (real_type v) const { throw Exception ("operation not supported!"); return v; }
    complex_type Z (complex_type v) const { throw Exception ("operation not supported!"); return v; }
};


class OpBinary : public OpBase {
  public:
    OpBinary (const char* format_string, bool complex_maps_to_real = false, bool real_map_to_complex = false) :
      OpBase (format_string, complex_maps_to_real, real_map_to_complex) { }
    complex_type R (real_type a, real_type b) const { throw Exception ("operation not supported!"); return a; }
    complex_type Z (complex_type a, complex_type b) const { throw Exception ("operation not supported!"); return a; }
};

class OpTernary : public OpBase {
  public:
    OpTernary (const char* format_string, bool complex_maps_to_real = false, bool real_map_to_complex = false) :
      OpBase (format_string, complex_maps_to_real, real_map_to_complex) { }
    complex_type R (real_type a, real_type b, real_type c) const { throw Exception ("operation not supported!"); return a; }
    complex_type Z (complex_type a, complex_type b, complex_type c) const { throw Exception ("operation not supported!"); return a; }
};



/**********************************************************************
        UNARY OPERATIONS:
**********************************************************************/

class OpAbs : public OpUnary {
  public:
    OpAbs () : OpUnary ("|%1|", true) { }
    complex_type R (real_type v) const { return std::abs (v); }
    complex_type Z (complex_type v) const { return std::abs (v); }
};

class OpNeg : public OpUnary {
  public:
    OpNeg () : OpUnary ("-%1") { }
    complex_type R (real_type v) const { return -v; }
    complex_type Z (complex_type v) const { return -v; }
};

class OpSqrt : public OpUnary {
  public:
    OpSqrt () : OpUnary ("sqrt (%1)") { } 
    complex_type R (real_type v) const { return std::sqrt (v); }
    complex_type Z (complex_type v) const { return std::sqrt (v); }
};

class OpExp : public OpUnary {
  public:
    OpExp () : OpUnary ("exp (%1)") { }
    complex_type R (real_type v) const { return std::exp (v); }
    complex_type Z (complex_type v) const { return std::exp (v); }
};

class OpLog : public OpUnary {
  public:
    OpLog () : OpUnary ("log (%1)") { }
    complex_type R (real_type v) const { return std::log (v); }
    complex_type Z (complex_type v) const { return std::log (v); }
};

class OpLog10 : public OpUnary {
  public:
    OpLog10 () : OpUnary ("log10 (%1)") { }
    complex_type R (real_type v) const { return std::log10 (v); }
    complex_type Z (complex_type v) const { return std::log10 (v); }
};

class OpCos : public OpUnary {
  public:
    OpCos () : OpUnary ("cos (%1)") { } 
    complex_type R (real_type v) const { return std::cos (v); }
    complex_type Z (complex_type v) const { return std::cos (v); }
};

class OpSin : public OpUnary {
  public:
    OpSin () : OpUnary ("sin (%1)") { } 
    complex_type R (real_type v) const { return std::sin (v); }
    complex_type Z (complex_type v) const { return std::sin (v); }
};

class OpTan : public OpUnary {
  public:
    OpTan () : OpUnary ("tan (%1)") { }
    complex_type R (real_type v) const { return std::tan (v); }
    complex_type Z (complex_type v) const { return std::tan (v); }
};

class OpCosh : public OpUnary {
  public:
    OpCosh () : OpUnary ("cosh (%1)") { }
    complex_type R (real_type v) const { return std::cosh (v); }
    complex_type Z (complex_type v) const { return std::cosh (v); }
};

class OpSinh : public OpUnary {
  public:
    OpSinh () : OpUnary ("sinh (%1)") { }
    complex_type R (real_type v) const { return std::sinh (v); }
    complex_type Z (complex_type v) const { return std::sinh (v); }
};

class OpTanh : public OpUnary {
  public:
    OpTanh () : OpUnary ("tanh (%1)") { } 
    complex_type R (real_type v) const { return std::tanh (v); }
    complex_type Z (complex_type v) const { return std::tanh (v); }
};

class OpAcos : public OpUnary {
  public:
    OpAcos () : OpUnary ("acos (%1)") { }
    complex_type R (real_type v) const { return std::acos (v); }
};

class OpAsin : public OpUnary {
  public:
    OpAsin () : OpUnary ("asin (%1)") { } 
    complex_type R (real_type v) const { return std::asin (v); }
};

class OpAtan : public OpUnary {
  public:
    OpAtan () : OpUnary ("atan (%1)") { }
    complex_type R (real_type v) const { return std::atan (v); }
};

class OpAcosh : public OpUnary {
  public:
    OpAcosh () : OpUnary ("acosh (%1)") { } 
    complex_type R (real_type v) const { return std::acosh (v); }
};

class OpAsinh : public OpUnary {
  public:
    OpAsinh () : OpUnary ("asinh (%1)") { }
    complex_type R (real_type v) const { return std::asinh (v); }
};

class OpAtanh : public OpUnary {
  public:
    OpAtanh () : OpUnary ("atanh (%1)") { }
    complex_type R (real_type v) const { return std::atanh (v); }
};


class OpRound : public OpUnary {
  public:
    OpRound () : OpUnary ("round (%1)") { } 
    complex_type R (real_type v) const { return std::round (v); }
};

class OpCeil : public OpUnary {
  public:
    OpCeil () : OpUnary ("ceil (%1)") { } 
    complex_type R (real_type v) const { return std::ceil (v); }
};

class OpFloor : public OpUnary {
  public:
    OpFloor () : OpUnary ("floor (%1)") { }
    complex_type R (real_type v) const { return std::floor (v); }
};

class OpReal : public OpUnary {
  public:
    OpReal () : OpUnary ("real (%1)", true) { }
    complex_type Z (complex_type v) const { return v.real(); }
};

class OpImag : public OpUnary {
  public:
    OpImag () : OpUnary ("imag (%1)", true) { }
    complex_type Z (complex_type v) const { return v.imag(); }
};

class OpPhase : public OpUnary {
  public:
    OpPhase () : OpUnary ("phase (%1)", true) { }
    complex_type Z (complex_type v) const { return std::arg (v); }
};

class OpConj : public OpUnary {
  public:
    OpConj () : OpUnary ("conj (%1)") { } 
    complex_type Z (complex_type v) const { return std::conj (v); }
};

class OpIsNaN : public OpUnary {
  public:
    OpIsNaN () : OpUnary ("isnan (%1)", true, false) { }
    complex_type R (real_type v) const { return std::isnan (v) != 0; }
    complex_type Z (complex_type v) const { return std::isnan (v.real()) != 0 || std::isnan (v.imag()) != 0; }
};

class OpIsInf : public OpUnary {
  public:
    OpIsInf () : OpUnary ("isinf (%1)", true, false) { }
    complex_type R (real_type v) const { return std::isinf (v) != 0; }
    complex_type Z (complex_type v) const { return std::isinf (v.real()) != 0 || std::isinf (v.imag()) != 0; }
};

class OpFinite : public OpUnary {
  public:
    OpFinite () : OpUnary ("finite (%1)", true, false) { }
    complex_type R (real_type v) const { return std::isfinite (v) != 0; }
    complex_type Z (complex_type v) const { return std::isfinite (v.real()) != 0|| std::isfinite (v.imag()) != 0; }
};


/**********************************************************************
        BINARY OPERATIONS:
**********************************************************************/

class OpAdd : public OpBinary {
  public:
    OpAdd () : OpBinary ("(%1 + %2)") { } 
    complex_type R (real_type a, real_type b) const { return a+b; }
    complex_type Z (complex_type a, complex_type b) const { return a+b; }
};

class OpSubtract : public OpBinary {
  public:
    OpSubtract () : OpBinary ("(%1 - %2)") { } 
    complex_type R (real_type a, real_type b) const { return a-b; }
    complex_type Z (complex_type a, complex_type b) const { return a-b; }
};

class OpMultiply : public OpBinary {
  public:
    OpMultiply () : OpBinary ("(%1 * %2)") { }
    complex_type R (real_type a, real_type b) const { return a*b; }
    complex_type Z (complex_type a, complex_type b) const { return a*b; }
};

class OpDivide : public OpBinary {
  public:
    OpDivide () : OpBinary ("(%1 / %2)") { } 
    complex_type R (real_type a, real_type b) const { return a/b; }
    complex_type Z (complex_type a, complex_type b) const { return a/b; }
};

class OpPow : public OpBinary {
  public:
    OpPow () : OpBinary ("%1^%2") { }
    complex_type R (real_type a, real_type b) const { return std::pow (a, b); }
    complex_type Z (complex_type a, complex_type b) const { return std::pow (a, b); }
};

class OpMin : public OpBinary {
  public:
    OpMin () : OpBinary ("min (%1, %2)") { }
    complex_type R (real_type a, real_type b) const { return std::min (a, b); }
};

class OpMax : public OpBinary {
  public:
    OpMax () : OpBinary ("max (%1, %2)") { } 
    complex_type R (real_type a, real_type b) const { return std::max (a, b); }
};

class OpLessThan : public OpBinary {
  public:
    OpLessThan () : OpBinary ("(%1 < %2)") { }
    complex_type R (real_type a, real_type b) const { return a < b; }
};

class OpGreaterThan : public OpBinary {
  public:
    OpGreaterThan () : OpBinary ("(%1 > %2)") { } 
    complex_type R (real_type a, real_type b) const { return a > b; }
};

class OpLessThanOrEqual : public OpBinary {
  public:
    OpLessThanOrEqual () : OpBinary ("(%1 <= %2)") { }
    complex_type R (real_type a, real_type b) const { return a <= b; }
};

class OpGreaterThanOrEqual : public OpBinary {
  public:
    OpGreaterThanOrEqual () : OpBinary ("(%1 >= %2)") { } 
    complex_type R (real_type a, real_type b) const { return a >= b; }
};

class OpEqual : public OpBinary {
  public:
    OpEqual () : OpBinary ("(%1 == %2)", true) { }
    complex_type R (real_type a, real_type b) const { return a == b; }
    complex_type Z (complex_type a, complex_type b) const { return a == b; }
};

class OpNotEqual : public OpBinary {
  public:
    OpNotEqual () : OpBinary ("(%1 != %2)", true) { }
    complex_type R (real_type a, real_type b) const { return a != b; }
    complex_type Z (complex_type a, complex_type b) const { return a != b; }
};

class OpComplex : public OpBinary {
  public:
    OpComplex () : OpBinary ("(%1 + %2 i)", false, true) { }
    complex_type R (real_type a, real_type b) const { return complex_type (a, b); }
};







/**********************************************************************
        TERNARY OPERATIONS:
**********************************************************************/

class OpIf : public OpTernary {
  public:
    OpIf () : OpTernary ("(%1 ? %2 : %3)") { }
    complex_type R (real_type a, real_type b, real_type c) const { return a ? b : c; }
    complex_type Z (complex_type a, complex_type b, complex_type c) const { return a.real() ? b : c; }
};



/**********************************************************************
  MAIN BODY OF COMMAND:
 **********************************************************************/

void run () {
  std::vector<StackEntry> stack;

  for (int n = 1; n < App::argc; ++n) {

    const Option* opt = match_option (App::argv[n]);
    if (opt) {
      if (opt->is ("abs")) unary_operation (opt->id, stack, OpAbs()); 
      else if (opt->is ("neg")) unary_operation (opt->id, stack, OpNeg());
      else if (opt->is ("sqrt")) unary_operation (opt->id, stack, OpSqrt());
      else if (opt->is ("exp")) unary_operation (opt->id, stack, OpExp());
      else if (opt->is ("log")) unary_operation (opt->id, stack, OpLog());
      else if (opt->is ("log10")) unary_operation (opt->id, stack, OpLog10());

      else if (opt->is ("cos")) unary_operation (opt->id, stack, OpCos());
      else if (opt->is ("sin")) unary_operation (opt->id, stack, OpSin());
      else if (opt->is ("tan")) unary_operation (opt->id, stack, OpTan());

      else if (opt->is ("cosh")) unary_operation (opt->id, stack, OpCosh());
      else if (opt->is ("sinh")) unary_operation (opt->id, stack, OpSinh());
      else if (opt->is ("tanh")) unary_operation (opt->id, stack, OpTanh());

      else if (opt->is ("acos")) unary_operation (opt->id, stack, OpAcos());
      else if (opt->is ("asin")) unary_operation (opt->id, stack, OpAsin());
      else if (opt->is ("atan")) unary_operation (opt->id, stack, OpAtan());

      else if (opt->is ("acosh")) unary_operation (opt->id, stack, OpAcosh());
      else if (opt->is ("asinh")) unary_operation (opt->id, stack, OpAsinh());
      else if (opt->is ("atanh")) unary_operation (opt->id, stack, OpAtanh());

      else if (opt->is ("round")) unary_operation (opt->id, stack, OpRound());
      else if (opt->is ("ceil")) unary_operation (opt->id, stack, OpCeil());
      else if (opt->is ("floor")) unary_operation (opt->id, stack, OpFloor());

      else if (opt->is ("real")) unary_operation (opt->id, stack, OpReal());
      else if (opt->is ("imag")) unary_operation (opt->id, stack, OpImag());
      else if (opt->is ("phase")) unary_operation (opt->id, stack, OpPhase());
      else if (opt->is ("conj")) unary_operation (opt->id, stack, OpConj());

      else if (opt->is ("isnan")) unary_operation (opt->id, stack, OpIsNaN());
      else if (opt->is ("isinf")) unary_operation (opt->id, stack, OpIsInf());
      else if (opt->is ("finite")) unary_operation (opt->id, stack, OpFinite());

      else if (opt->is ("add")) binary_operation (opt->id, stack, OpAdd());
      else if (opt->is ("subtract")) binary_operation (opt->id, stack, OpSubtract());
      else if (opt->is ("multiply")) binary_operation (opt->id, stack, OpMultiply());
      else if (opt->is ("divide")) binary_operation (opt->id, stack, OpDivide());
      else if (opt->is ("pow")) binary_operation (opt->id, stack, OpPow());

      else if (opt->is ("min")) binary_operation (opt->id, stack, OpMin());
      else if (opt->is ("max")) binary_operation (opt->id, stack, OpMax());
      else if (opt->is ("lt")) binary_operation (opt->id, stack, OpLessThan());
      else if (opt->is ("gt")) binary_operation (opt->id, stack, OpGreaterThan());
      else if (opt->is ("le")) binary_operation (opt->id, stack, OpLessThanOrEqual());
      else if (opt->is ("ge")) binary_operation (opt->id, stack, OpGreaterThanOrEqual());
      else if (opt->is ("eq")) binary_operation (opt->id, stack, OpEqual());
      else if (opt->is ("neq")) binary_operation (opt->id, stack, OpNotEqual());

      else if (opt->is ("complex")) binary_operation (opt->id, stack, OpComplex());

      else if (opt->is ("if")) ternary_operation (opt->id, stack, OpIf());

      else if (opt->is ("datatype")) ++n;
      else if (opt->is ("nthreads")) ++n;
      else if (opt->is ("force") || opt->is ("info") || opt->is ("debug") || opt->is ("quiet"))
        continue;

      else 
        throw Exception (std::string ("operation \"") + opt->id + "\" not yet implemented!");

    }
    else {
      stack.push_back (App::argv[n]);
    }

  }

  stack[0].load();
  run_operations (stack);
}



