#pragma once

#include <cstddef>
#include <vector>
#include <deque>
#include <string>
#include <map>
#include <fstream>
#include <iostream>
#include <memory>
#include <filesystem>
#include <libgen.h>
#include <binaryen-c.h>
#include "../parser/ast/ASTNode.hpp"
#include "../parser/ast/LinkNode.hpp"
#include "exceptions/Error.hpp"
#include "TypeChecker.hpp"
#include "CodeGen.hpp"
#include "compiler/optimization/OptimizationPass.hpp"
#include "compiler/optimization/LiteralInlinerPass.hpp"
#include "parser/ast/TypeDeclarationNode.hpp"

using namespace std;

/**
 * @brief Singleton class responsible for compiling Theta source code into an Abstract Syntax Tree (AST).
 */
namespace Theta {
    class Compiler {
        public:
            /**
             * @brief Compiles the Theta source code starting from the specified entry point.
             * @param entrypoint The entry point file name or identifier.
             * @param outputFile The output file which will be the result of the compilation
             * @param isEmitTokens Toggles whether or not the lexer tokens should be output to the console
             * @param isEmitAST Toggles whether or not the AST should be output to the console
             */
            void compile(string entrypoint, string outputFile, bool isEmitTokens = false, bool isEmitAST = false, bool isEmitWAT = false);

            /**
             * @brief Compiles the Theta source code starting from the specified entry point.
             * @param source The source code to compile.
             * @return A shared pointer to the root node of the constructed AST
             */
            shared_ptr<Theta::ASTNode> compileDirect(string source);

            /**
             * @brief Builds the Abstract Syntax Tree (AST) for the Theta source code starting from the specified file.
             * @param fileName The file name of the Theta source code.
             * @return A shared pointer to the root node of the constructed AST.
             */
            shared_ptr<Theta::ASTNode> buildAST(string fileName);

            /**
             * @brief Builds the Abstract Syntax Tree (AST) for the Theta source code provided.
             * @param source The source code to compile.
             * @param fileName The file name of the Theta source code.
             * @return A shared pointer to the root node of the constructed AST.
             */
            shared_ptr<Theta::ASTNode> buildAST(string source, string fileName);

            /**
             * @brief Gets the singleton instance of the Compiler.
             * @return Reference to the singleton instance of Compiler.
             */
            static Compiler& getInstance();

            /**
             * @brief Adds an encountered exception to the list of exceptions to display later
             * @param e The exception to add
             */
            void addException(shared_ptr<Theta::Error> e);

            /**
             * @brief Returns all the exceptions we encountered during the compilation process
             * @return A vector of compilation errors
             */
            vector<shared_ptr<Theta::Error>> getEncounteredExceptions();

            /**
             * @brief Clears the list of compilation errors
             */
            void clearExceptions();

            /**
             * @brief Returns a LinkNode for a given capsule name, if it exists
             * @param capsuleName The name of the capsule
             * @return A shared pointer to the LinkNode containing the parsed AST
             */
            shared_ptr<Theta::LinkNode> getIfExistsParsedLinkAST(string capsuleName);

            /**
             * @brief Adds a LinkNode to the map of parsed capsule ASTs
             * @param capsuleName The name of the capsule
             * @param linkNode A shared pointer to the LinkNode to add
             */
            void addParsedLinkAST(string capsuleName, shared_ptr<Theta::LinkNode> linkNode);
            
            /**
             * @brief Runs optimization passes on the AST (in-place)
             * @param The AST to optimize
             * @return true If all optimization passes succeeded
             */
            bool optimizeAST(shared_ptr<ASTNode> &ast, bool silenceErrors = false);

            
            /**
             * @brief Generates a unique function identifier based on the function's name and its parameters to handle overloading.
             * 
             * @param variableName The base name of the function.
             * @param declarationNode The function declaration node containing the parameters.
             * @return string The unique identifier for the function.
             */
            static string getQualifiedFunctionIdentifier(string variableName, shared_ptr<ASTNode> node);
    
            /**
             * @brief Generates a unique function identifier based on the function's name and its type signature
             * 
             * @param variableName The base name of the function.
             * @param typeSig The type signature of the function.
             * @return string The unique identifier for the function.
             */
            static string getQualifiedFunctionIdentifierFromTypeSignature(string variableName, shared_ptr<TypeDeclarationNode> typeSig);

            /**
             * @brief Finds all AST nodes of a specific type within the tree rooted at a given node.
             * 
             * @param node The root node to search from.
             * @param type The type of nodes to find.
             * @return vector<shared_ptr<ASTNode>> A vector of found nodes.
             */
            static vector<shared_ptr<ASTNode>> findAllInTree(shared_ptr<ASTNode> node, ASTNode::Types type);

            /**
             * @brief Creates a deep copy of a type declaration node, useful for cases where type information 
             * needs to be duplicated without referencing the original.
             * 
             * @param original The original type declaration node to copy.
             * @return shared_ptr<TypeDeclarationNode> The deep-copied type declaration node.
             */
            static shared_ptr<TypeDeclarationNode> deepCopyTypeDeclaration(shared_ptr<TypeDeclarationNode> node, shared_ptr<ASTNode> parent);

            static vector<char> writeModuleToBuffer(BinaryenModuleRef &module);

            shared_ptr<map<string, string>> filesByCapsuleName;

            static string resolveAbsolutePath(string relativePath);
        private:
            /**
             * @brief Private constructor for Compiler. Initializes the compiler and discovers all capsules in the source files.
             */
            Compiler() {
                filesByCapsuleName = make_shared<map<string, string>>();
                discoverCapsules();

                optimizationPasses = {
                    make_shared<LiteralInlinerPass>()
                };
            }

            // Delete copy constructor and assignment operator to enforce singleton pattern
            Compiler(const Compiler&) = delete;
            Compiler& operator=(const Compiler&) = delete;


            bool isEmitTokens = false;
            bool isEmitAST = false;
            bool isEmitWAT = false;
            vector<shared_ptr<Theta::Error>> encounteredExceptions;
            map<string, shared_ptr<Theta::LinkNode>> parsedLinkASTs;

            vector<shared_ptr<OptimizationPass>> optimizationPasses; 

            /**
             * @brief Outputs the contents of a given WASM module to the given file
             * @param module The module to write
             * @param file The filename to write the module to
             */
            void writeModuleToFile(BinaryenModuleRef &module, string file);

            /**
             * @brief Discovers all capsules in the Theta source code.
             *
             * Scans the current directory and subdirectories to find all `.th` files and extracts capsule names.
             */
            void discoverCapsules();

            /**
             * @brief Finds the capsule name associated with the given file.
             *
             * Reads the content of the file and searches for the `capsule` keyword to identify the capsule name.
             *
             * @param file The file for which to find the capsule name.
             * @return The capsule name corresponding to the file.
             */
            string findCapsuleName(string file);

            /**
             * @brief Outputs a given AST to STDOUT
             * @param ast The AST to output
             * @param fileName The filename that appears as the "Source file" for the ast
             */
            void outputAST(shared_ptr<ASTNode> ast, string fileName);
    };
}
