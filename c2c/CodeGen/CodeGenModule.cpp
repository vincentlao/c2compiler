/* Copyright 2013,2014 Bas van den Berg
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <vector>
// TODO REMOVE
#include <stdio.h>

#include <llvm/IR/Module.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
// for SmallString
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/FileSystem.h>
// for tool_output_file
#include <llvm/Support/ToolOutputFile.h>
// for WriteBitcodeToFile
#include <llvm/Bitcode/ReaderWriter.h>
// For verifyModule()
#include <llvm/Analysis/Verifier.h>

#include "CodeGen/CodeGenModule.h"
#include "CodeGen/CodeGenFunction.h"
#include "AST/Package.h"
#include "AST/AST.h"
#include "AST/Decl.h"
#include "AST/Expr.h"
#include "Utils/StringBuilder.h"

//#define DEBUG_CODEGEN

using namespace C2;
using namespace llvm;

CodeGenModule::CodeGenModule(const std::string& name_, bool single)
    : name(name_)
    , single_module(single)
    , context(llvm::getGlobalContext())
    , module(new llvm::Module(name, context))
    , builder(context)
{}

CodeGenModule::~CodeGenModule() {
    delete module;
}

void CodeGenModule::generate() {
    // step 1: generate all function proto's
#if 0
    // TEMP hardcode type Point struct { int x, int y }
    std::vector<llvm::Type *> elems;
    elems.push_back(builder.getInt32Ty());
    elems.push_back(builder.getInt32Ty());
    llvm::ArrayRef<llvm::Type*>  Elements(elems);
    llvm::StructType* ST = llvm::StructType::create(context, Elements, "struct.Point", false);

    // TEMP Hardcode Point p = { 10, 20 }
    llvm::GlobalValue::LinkageTypes ltype = llvm::GlobalValue::ExternalLinkage;
    llvm::Constant* init = 0; //llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 20, true);
    // TODO generate { i32 10, i32 20 }
    // see clang:: EmitRecordInitialization()

    new llvm::GlobalVariable(*module, ST, false, ltype, init, "p");
#endif

    for (EntriesIter iter = entries.begin(); iter != entries.end(); ++iter) {
        const AST* ast = *iter;
#ifdef DEBUG_CODEGEN
        printf("CodeGen for %s - step 1\n", ast->getFileName().c_str());
#endif
        for (unsigned i=0; i<ast->numFunctions(); i++) {
            FunctionDecl* F = ast->getFunction(i);
            CodeGenFunction cgf(*this, F);
            llvm::Function* proto = cgf.generateProto(F->getPackage()->getCName());
            F->setIRProto(proto);
        }
    }
    // step 2: generate variables
    for (EntriesIter iter = entries.begin(); iter != entries.end(); ++iter) {
        const AST* ast = *iter;
#ifdef DEBUG_CODEGEN
        printf("CodeGen for %s - step 1\n", ast->getFileName().c_str());
#endif
        for (unsigned i=0; i<ast->numVars(); i++) {
            EmitGlobalVariable(ast->getVar(i));
        }
    }

    // step 3: generate all function bodies (and possibly external function decls)
    for (EntriesIter iter = entries.begin(); iter != entries.end(); ++iter) {
        const AST* ast = *iter;
#ifdef DEBUG_CODEGEN
        printf("CodeGen for %s - step 1\n", ast->getFileName().c_str());
#endif
        for (unsigned i=0; i<ast->numFunctions(); i++) {
            FunctionDecl* F = ast->getFunction(i);
            CodeGenFunction cgf(*this, F);
            cgf.generateBody(F->getIRProto());
        }
    }
}

bool CodeGenModule::verify() {
    if (verifyModule(*module)) {
        errs() << "Error in Module!\n";
        return false;
    }
    return true;
}

void CodeGenModule::dump() {
    fprintf(stdout, "-----------------------------------------------------------------------\n");
    module->print(llvm::outs(), 0);
}

void CodeGenModule::write(const std::string& target, const std::string& name) {
    // write IR Module to output/<target>/<package>.ll
    StringBuilder filename;
    filename << "output/" << target << '/';
    bool existed;
    llvm::Twine path(filename);
    if (llvm::sys::fs::create_directories(path, existed) != llvm::errc::success) {
        fprintf(stderr, "Could not create directory: %s\n", (const char*)filename);
        return;
    }

    filename << name << ".ll";
    std::string ErrorInfo;
    llvm::raw_fd_ostream OS((const char*)filename, ErrorInfo);
    if (!ErrorInfo.empty()) {
        fprintf(stderr, "%s\n", ErrorInfo.c_str());
        return;
    }
    module->print(OS, 0);
    printf("written %s\n", (const char*)filename);
#if 0
    // print to binary file
    {
        std::string ErrInfo;
        StringBuilder filename;
        filename << "output/" << target << '/' << name;
        const char* outfile = (const char*)filename;
        tool_output_file Out(outfile, ErrInfo, raw_fd_ostream::F_Binary);
        if (!ErrInfo.empty()) {
            fprintf(stderr, "Could not open file %s\n", outfile);
            return;
        }
        llvm::WriteBitcodeToFile(module, Out.os());
        Out.os().close();
        if (Out.os().has_error()) {
            fprintf(stderr, "Error writing file %s\n", outfile);
        }
        Out.keep();
        printf("written %s\n", outfile);
    }
#endif
}

void CodeGenModule::EmitGlobalVariable(VarDecl* Var) {
    //QualType qt = Var->getType();
    //llvm::Type* type = ConvertType(qt.getTypePtr());
    bool constant = false;
    llvm::GlobalValue::LinkageTypes ltype = getLinkage(Var->isPublic());
    // TODO use correct arguments for constant and Initializer
    // NOTE: getName() doesn't have to be virtual here
    // TODO is var is array and has bool isIncrementalArray also generate init code

    const Expr* I = Var->getInitValue();
    llvm::Constant* init;
    if (I) {
        init = EvaluateExprAsConstant(I);
    } else {
        // ALWAYS initialize globals
        // TODO dynamic width
        init = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0, true);
    }
    //new llvm::GlobalVariable(*module, type, constant, ltype, init, Var->getName());
    // Set alignment based on Type

    llvm::GlobalVariable* GV = new llvm::GlobalVariable(*module, init->getType(), constant, ltype, init, Var->getName());
    GV->setAlignment(getAlignment(Var->getType()));
}

// TODO remove
void CodeGenModule::EmitTopLevelDecl(Decl* D) {
    switch (D->getKind()) {
    case DECL_FUNC:
    case DECL_VAR:
    case DECL_ENUMVALUE:
        assert(0);
        break;
    case DECL_ALIASTYPE:
        {
            //AliasTypeDecl* A = cast<AliasTypeDecl>(D);
            //QualType QT = A->getType();
        }
        break;
    case DECL_STRUCTTYPE:
        {
            //StructTypeDecl* TD = cast<StructTypeDecl>(D);
            //QualType QT = TD->getType();
            // NOTE: only generate code for struct/union types (even this is optional)
#if 0
            if (QT.isStructOrUnionType()) {
                // TEMP try some ints
                std::vector<llvm::Type *> putsArgs;
                putsArgs.push_back(builder.getInt32Ty());
                llvm::ArrayRef<llvm::Type*>  argsRef(putsArgs);
                llvm::StructType* s = llvm::StructType::create(getContext(), putsArgs, TD->getName());
                //const Type* T = QT.getTypePtr();
            }
#endif
        }
        break;
    case DECL_ENUMTYPE:
    case DECL_FUNCTIONTYPE:
        assert(0);
        break;
    case DECL_ARRAYVALUE:
        assert(0 && "TODO arrayvalue");
        break;
    case DECL_USE:
        // nothing needed
        break;
    }
}

unsigned CodeGenModule::getAlignment(QualType Q) const {
    const Type* T = Q.getCanonicalType();
    switch (T->getTypeClass()) {
    case TC_BUILTIN:
        return cast<BuiltinType>(T)->getWidth() / 8;
    case TC_POINTER:
        return 4; // TEMP
    case TC_ARRAY:
        return getAlignment(cast<ArrayType>(T)->getElementType());
    case TC_UNRESOLVED:
    case TC_ALIAS:
        assert(0);
        break;
    case TC_STRUCT:
        assert(0 && "TODO");
        break;
    case TC_ENUM:
        assert(0);
        break;
    case TC_FUNCTION:
        assert(0 && "TODO");
        break;
    case TC_PACKAGE:
        assert(0);
        break;
    }
    return 0;
}

llvm::Type* CodeGenModule::ConvertType(BuiltinType::Kind K) {
    switch (K) {
    // TODO make types signed or not
    case BuiltinType::Int8:      return builder.getInt8Ty();
    case BuiltinType::Int16:     return builder.getInt16Ty();
    case BuiltinType::Int32:     return builder.getInt32Ty();
    case BuiltinType::Int64:     return builder.getInt64Ty();
    case BuiltinType::UInt8:     return builder.getInt8Ty();
    case BuiltinType::UInt16:    return builder.getInt16Ty();
    case BuiltinType::UInt32:    return builder.getInt32Ty();
    case BuiltinType::UInt64:    return builder.getInt64Ty();
    case BuiltinType::Float32:   return builder.getFloatTy();
    case BuiltinType::Float64:   return builder.getFloatTy(); // TODO make double
    case BuiltinType::Bool:      return builder.getInt1Ty();
    case BuiltinType::Void:      return builder.getVoidTy();
    }
    return 0;   // satisfy compiler
}

llvm::Type* CodeGenModule::ConvertType(QualType Q) {
    const C2::Type* canon = Q.getCanonicalType();
    switch (canon->getTypeClass()) {
    case TC_BUILTIN:
        return ConvertType(cast<BuiltinType>(canon)->getKind());
    case TC_POINTER:
        {
            llvm::Type* tt = ConvertType(cast<PointerType>(canon)->getPointeeType().getTypePtr());
            return tt->getPointerTo();
        }
    case TC_ARRAY:
        {
            // Hmm for function args, array are simply converted to pointers, do that for now
            // array: use type = ArrayType::get(elementType, numElements)
            llvm::Type* tt = ConvertType(cast<ArrayType>(canon)->getElementType().getTypePtr());
            return tt->getPointerTo();
        }
    case TC_UNRESOLVED:
    case TC_ALIAS:
        assert(0 && "should be resolved");
        break;
    case TC_STRUCT:
        assert(0 && "TODO union/struct type");
        break;
    case TC_ENUM:
        // TODO use canonical type, for now use Int32()
        return builder.getInt32Ty();
    case TC_FUNCTION:
        assert(0 && "TODO func type");
        break;
    case TC_PACKAGE:
        assert(0 && "TODO");
        break;
    }
    return 0;
}

llvm::Function* CodeGenModule::createExternal(const Package* P, const std::string& name) {
    Decl* D = P->findSymbol(name);
    assert(D);
    FunctionDecl* F = cast<FunctionDecl>(D);
    CodeGenFunction cgf(*this, F);
    llvm::Function* proto = cgf.generateProto(P->getCName());
    return proto;
}

llvm::GlobalValue::LinkageTypes CodeGenModule::getLinkage(bool isPublic) {
    if (isPublic && !single_module) return llvm::GlobalValue::ExternalLinkage;
    else return llvm::GlobalValue::InternalLinkage;
}

llvm::Constant* CodeGenModule::EvaluateExprAsConstant(const Expr *E) {
    switch (E->getKind()) {
    case EXPR_STRING_LITERAL:
        return GetConstantArrayFromStringLiteral(cast<StringLiteral>(E));
    case EXPR_INTEGER_LITERAL:
        {
            const IntegerLiteral* N = cast<IntegerLiteral>(E);
            // Get Width/signed from CanonicalType?
            return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), N->Value.getSExtValue(), true);
        }
    case EXPR_INITLIST:
        {
            // TODO only use this for arrays of Builtins
            const InitListExpr* I = cast<InitListExpr>(E);
            QualType Q = I->getType().getCanonicalType();
            // NOTE: we only support array inits currently (no struct inits yet)
            assert(Q.isArrayType());
            const ArrayType* AT = cast<ArrayType>(Q.getTypePtr());
            QualType ET = AT->getElementType();
            assert(isa<BuiltinType>(ET.getTypePtr()));
            BuiltinType* BT = cast<BuiltinType>(ET.getTypePtr());
            // TEMP assume int32
            // TODO get size from AT->getElement..
            const ExprList& Vals = I->getValues();
            switch (BT->getWidth()) {
            case 8:
            {
                SmallVector<uint8_t, 32> Elements;
                for (unsigned i=0; i<Vals.size(); i++) {
                    const Expr* elem = Vals[i];
                    assert(isa<IntegerLiteral>(elem));
                    const IntegerLiteral* N = cast<IntegerLiteral>(elem);
                    Elements.push_back((uint8_t)N->Value.getZExtValue());
                }
                return llvm::ConstantDataArray::get(context, Elements);
            }
            case 16:
            {
                SmallVector<uint16_t, 32> Elements;
                for (unsigned i=0; i<Vals.size(); i++) {
                    const Expr* elem = Vals[i];
                    assert(isa<IntegerLiteral>(elem));
                    const IntegerLiteral* N = cast<IntegerLiteral>(elem);
                    Elements.push_back((uint16_t)N->Value.getZExtValue());
                }
                return llvm::ConstantDataArray::get(context, Elements);
            }
            case 32:
            {
                SmallVector<uint32_t, 32> Elements;
                for (unsigned i=0; i<Vals.size(); i++) {
                    const Expr* elem = Vals[i];
                    assert(isa<IntegerLiteral>(elem));
                    const IntegerLiteral* N = cast<IntegerLiteral>(elem);
                    Elements.push_back((uint32_t)N->Value.getZExtValue());
                }
                return llvm::ConstantDataArray::get(context, Elements);
            }
            case 64:
            {
                SmallVector<uint64_t, 32> Elements;
                for (unsigned i=0; i<Vals.size(); i++) {
                    const Expr* elem = Vals[i];
                    assert(isa<IntegerLiteral>(elem));
                    const IntegerLiteral* N = cast<IntegerLiteral>(elem);
                    Elements.push_back((uint64_t)N->Value.getZExtValue());
                }
                return llvm::ConstantDataArray::get(context, Elements);
            }
            default:
                assert(0 && "TODO?");
                return 0;
            }
        }
    default:
        E->dump();
        assert(0 && "TODO");
        return 0;
    }
}

llvm::Constant* CodeGenModule::GetConstantArrayFromStringLiteral(const StringLiteral* E) {
  //assert(!E->getType()->isPointerType() && "Strings are always arrays");

    // TEMP only handle 1 byte per char
    SmallString<64> Str(E->value);
    Str.resize(E->value.size());
    //return llvm::ConstantDataArray::getString(context, Str, false);
    return llvm::ConstantDataArray::getString(context, Str, true); // add 0

#if 0
  // Don't emit it as the address of the string, emit the string data itself
  // as an inline array.
  if (E->getCharByteWidth() == 1) {
    SmallString<64> Str(E->getString());

    // Resize the string to the right size, which is indicated by its type.
    const ConstantArrayType *CAT = Context.getAsConstantArrayType(E->getType());
    Str.resize(CAT->getSize().getZExtValue());
    return llvm::ConstantDataArray::getString(VMContext, Str, false);
  }

  llvm::ArrayType *AType =
    cast<llvm::ArrayType>(getTypes().ConvertType(E->getType()));
  llvm::Type *ElemTy = AType->getElementType();
  unsigned NumElements = AType->getNumElements();

  // Wide strings have either 2-byte or 4-byte elements.
  if (ElemTy->getPrimitiveSizeInBits() == 16) {
    SmallVector<uint16_t, 32> Elements;
    Elements.reserve(NumElements);

    for(unsigned i = 0, e = E->getLength(); i != e; ++i)
      Elements.push_back(E->getCodeUnit(i));
    Elements.resize(NumElements);
    return llvm::ConstantDataArray::get(VMContext, Elements);
  }

  assert(ElemTy->getPrimitiveSizeInBits() == 32);
  SmallVector<uint32_t, 32> Elements;
  Elements.reserve(NumElements);

  for(unsigned i = 0, e = E->getLength(); i != e; ++i)
    Elements.push_back(E->getCodeUnit(i));
  Elements.resize(NumElements);
  return llvm::ConstantDataArray::get(VMContext, Elements);
#endif
}

