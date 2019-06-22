/* Copyright 2013-2019 Bas van den Berg
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

#include <assert.h>

#include "Analyser/ModuleAnalyser.h"
#include "Analyser/FileAnalyser.h"
#include "AST/ASTContext.h"

#include "Clang/Diagnostic.h"

using namespace C2;

ModuleAnalyser::ModuleAnalyser(Module& m_,
                               const Modules& allModules,
                               c2lang::DiagnosticsEngine& Diags_,
                               const TargetInfo& target_,
                               ASTContext& context_,
                               bool verbose_)
    : module(m_)
    , Diags(Diags_)
    , context(context_)
    , verbose(verbose_)
{
    // TODO lower analyser scope to analyse function (need to do unused checks in ComponentAnalyser
    const AstList& files = module.getFiles();
    for (unsigned f=0; f<files.size(); f++) {
        FileAnalyser* analyser = new FileAnalyser(module, allModules, Diags, target_, *files[f], verbose);
        analysers.push_back(analyser);
    }
}

ModuleAnalyser::~ModuleAnalyser() {
    for (auto &analyser : analysers) delete analyser;
}

unsigned ModuleAnalyser::analyse(bool print1, bool print2, bool print3, bool printLib) {
    unsigned errors = 0;
    const size_t count = analysers.size();

    // analyse globals
    for (unsigned i=0; i<count; i++) {
        analysers[i]->resolveTypes();
    }
    if (Diags.hasErrorOccurred()) return 1;

    for (unsigned i=0; i<count; i++) {
        errors += analysers[i]->resolveTypeCanonicals();
    }
    if (errors) return errors;

    for (unsigned i=0; i<count; i++) {
        errors += analysers[i]->resolveStructMembers();
    }
    if (print1) printASTs(printLib);
    if (errors) return errors;

    for (unsigned i=0; i<count; i++) {
        errors += analysers[i]->resolveVars();
    }
    if (errors) return errors;

    for (unsigned i=0; i<count; i++) {
        errors += analysers[i]->resolveEnumConstants();
    }
    if (errors) return errors;

    IncrementalArrayVals ia_values;
    for (unsigned i=0; i<count; i++) {
        errors += analysers[i]->checkArrayValues(ia_values);
    }
    if (errors) return errors;

    // Set ArrayValues
    for (IncrementalArrayValsIter iter = ia_values.begin(); iter != ia_values.end(); ++iter) {
        VarDecl* D = iter->first;
        unsigned numValues = iter->second.size();
        assert(numValues);
        // NOTE: incremenal array is given InitListExpr in resolveVars()
        Expr* I = D->getInitValue();
        assert(I);
        assert(dyncast<InitListExpr>(I));
        InitListExpr* ILE = cast<InitListExpr>(I);
        Expr** values = (Expr**)context.Allocate(sizeof(Expr*)*numValues);
        memcpy(values, &iter->second[0], sizeof(Expr*)*numValues);
        ILE->setValues(values, numValues);
    }
    ia_values.clear();

    StructFunctionList structFuncs;
    for (unsigned i=0; i<count; i++) {
        errors += analysers[i]->checkFunctionProtos(structFuncs);
    }
    if (errors) return errors;
    // Set StructFunctions
    // NOTE: since these are linked anyways, just use special ASTContext from Builder
    for (StructFunctionListIter iter = structFuncs.begin(); iter != structFuncs.end(); ++iter) {
        StructTypeDecl* S = iter->first;
        const StructFunctionEntries& entries = iter->second;
        unsigned numFuncs = entries.size();
        FunctionDecl** funcs = (FunctionDecl**)context.Allocate(sizeof(FunctionDecl*)*numFuncs);
        memcpy(funcs, &entries[0], sizeof(FunctionDecl*)*numFuncs);
        S->setStructFuncs(funcs, numFuncs);
    }

    for (unsigned i=0; i<count; i++) {
        analysers[i]->checkVarInits();
    }
    // NOTE: printASTs will be different, since a whole module is analysed at a time
    if (print2) printASTs(printLib);
    if (Diags.hasErrorOccurred()) return 1;

    // function bodies
    for (unsigned i=0; i<count; i++) {
        analysers[i]->checkFunctionBodies();
    }
    if (Diags.hasErrorOccurred()) return 1;

    if (print3) printASTs(printLib);
    return 0;
}

void ModuleAnalyser::checkUnused() {
    for (unsigned i=0; i<analysers.size(); i++) {
        analysers[i]->checkDeclsForUsed();
    }
}

void ModuleAnalyser::printASTs(bool printLib) const {
    const AstList& files = module.getFiles();
    for (unsigned f=0; f<files.size(); f++) {
        const AST* ast = files[f];
        // TODO invert logic, remove continue
        if (ast->isInterface() && !printLib) continue;
        ast->print(true);
    }
}

