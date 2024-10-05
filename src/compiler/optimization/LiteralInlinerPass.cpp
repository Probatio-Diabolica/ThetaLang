#include "LiteralInlinerPass.hpp"
#include "compiler/Compiler.hpp"
#include "compiler/DataTypes.hpp"
#include "exceptions/IllegalReassignmentError.hpp"
#include "parser/ast/ASTNodeList.hpp"
#include "parser/ast/BlockNode.hpp"
#include "parser/ast/EnumNode.hpp"
#include "parser/ast/IdentifierNode.hpp"
#include "parser/ast/LiteralNode.hpp"
#include "parser/ast/SymbolNode.hpp"
#include "parser/ast/TypeDeclarationNode.hpp"
#include <memory>

using namespace Theta;

// Finds any number literals that are stored in variables / enums and substitutes them with their literal value. This speeds up typechecking
void LiteralInlinerPass::optimizeAST(shared_ptr<ASTNode> &ast, bool isCapsuleDirectChild) {
  if (ast->getNodeType() == ASTNode::IDENTIFIER) {
    substituteIdentifiers(ast);
  } else if (ast->getNodeType() == ASTNode::TYPE_DECLARATION) {
    remapEnumTypeReferences(ast);
  } else if (ast->getNodeType() == ASTNode::ENUM) {
    unpackEnumElementsInScope(ast, localScope);

    ast = nullptr;
  } else if (ast->getNodeType() == ASTNode::ASSIGNMENT && !isCapsuleDirectChild) {
    bindIdentifierToScope(ast, localScope);

    // We dont want to remove variables defined directly in capsules, or if it is the last element in a block
    if (
      isLiteralAssignment(ast) && 
      !(
        ast->getParent()->getNodeType() == ASTNode::BLOCK &&
        dynamic_pointer_cast<BlockNode>(ast->getParent())->getElements().back()->getId() == ast->getId()
      )
    ) {
      ast = nullptr;
    }
  }
}

// When we have a variable being used somewhere (not assignment), we want to see if we can figure out what it referencing. If
// it is referencing a literal, we can just replace the variable with the literal to save time during typechecking and during runtime
void LiteralInlinerPass::substituteIdentifiers(shared_ptr<ASTNode> &ast) {
  // We only want to substitute identifiers that are not the LHS of an assignment
  if (ast->getValue() && ast->getValue()->getNodeType() == ASTNode::TYPE_DECLARATION) return;
  shared_ptr<IdentifierNode> ident = dynamic_pointer_cast<IdentifierNode>(ast);

  auto foundIdentifier = hoistedScope.lookup(ident->getIdentifier());

  auto foundInScope = localScope.lookup(ident->getIdentifier());
  if (foundInScope.has_value()) {
    foundIdentifier = foundInScope;
  }

  // Only optimize if we found the literal value we need to replace with
  if (
    !foundIdentifier.has_value() ||
    !(
      foundIdentifier.value()->getNodeType() == ASTNode::NUMBER_LITERAL ||
      foundIdentifier.value()->getNodeType() == ASTNode::STRING_LITERAL ||
      foundIdentifier.value()->getNodeType() == ASTNode::BOOLEAN_LITERAL
    )
  ) return;

  shared_ptr<LiteralNode> literal = dynamic_pointer_cast<LiteralNode>(foundIdentifier.value());

  ast = make_shared<LiteralNode>(literal->getNodeType(), literal->getLiteralValue(), ast);
}

// When we have a variable assigned to a literal, we can safely just add that to the scope
// since we know it references a primitive value
void LiteralInlinerPass::bindIdentifierToScope(shared_ptr<ASTNode> &ast, SymbolTableStack<shared_ptr<ASTNode>> &scope) {
  string identifier = dynamic_pointer_cast<IdentifierNode>(ast->getLeft())->getIdentifier();

  if (ast->getRight()->getNodeType() == ASTNode::FUNCTION_DECLARATION) {
    string uniqueFuncIdentifier = Compiler::getQualifiedFunctionIdentifier(identifier, ast->getRight());

    auto existingFuncIdentifierInScope = scope.lookup(uniqueFuncIdentifier);

    if (existingFuncIdentifierInScope.has_value()) {
      Compiler::getInstance().addException(make_shared<IllegalReassignmentError>(identifier));
      return;
    }

    scope.insert(uniqueFuncIdentifier, ast->getRight());
  } else {
    auto foundIdentInScope = scope.lookup(identifier);

    if (foundIdentInScope.has_value()) {
      Compiler::getInstance().addException(make_shared<IllegalReassignmentError>(identifier));
      return;
    }

    scope.insert(identifier, ast->getRight());
  }
}

void LiteralInlinerPass::hoistNecessary(shared_ptr<ASTNode> &ast) {
  shared_ptr<ASTNodeList> nodeList = dynamic_pointer_cast<ASTNodeList>(ast->getValue());

  vector<shared_ptr<ASTNode>> topLevelElements = nodeList->getElements();
  vector<int> removeAtIndices;

  hoistedScope.enterScope();

  for (int i = 0; i < topLevelElements.size(); i++) {
    if (topLevelElements.at(i)->getNodeType() == ASTNode::ENUM) {
      unpackEnumElementsInScope(topLevelElements.at(i), hoistedScope);

      // TODO: Revisit this when we do multi-capsule typechecking, we probably dont want to remove
      // any ast nodes that are hoisted because they could get referenced in another capsule. Maybe
      // we just need to persist the unpacked identifiers here instead
      removeAtIndices.push_back(i);
    } else if (topLevelElements.at(i)->getNodeType() == ASTNode::ASSIGNMENT) {
      bindIdentifierToScope(topLevelElements.at(i), hoistedScope);
    }
  }

  // Remove the enum node from the ast, it no longer exists since we've processed it
  for (int i = removeAtIndices.size() - 1; i >= 0; i--) {
    topLevelElements.erase(topLevelElements.begin() + removeAtIndices.at(i));
  }

  // Set the modified vector back
  nodeList->setElements(topLevelElements);
}

void LiteralInlinerPass::unpackEnumElementsInScope(shared_ptr<ASTNode> node, SymbolTableStack<shared_ptr<ASTNode>> &scope) {
  shared_ptr<EnumNode> enumNode = dynamic_pointer_cast<EnumNode>(node);
  string baseIdentifier = dynamic_pointer_cast<IdentifierNode>(enumNode->getIdentifier())->getIdentifier();
  vector<shared_ptr<ASTNode>> enumElements = dynamic_pointer_cast<ASTNodeList>(node)->getElements();

  for (int i = 0; i < enumElements.size(); i++) {
    shared_ptr<SymbolNode> elSymbol = dynamic_pointer_cast<SymbolNode>(enumElements.at(i));

    string enumElIdentifier = baseIdentifier + "." + elSymbol->getSymbol().substr(1);
    
    auto foundScopeIdentifier = scope.lookup(enumElIdentifier);
    if (foundScopeIdentifier) {
      Compiler::getInstance().addException(make_shared<IllegalReassignmentError>(enumElIdentifier));
      return;
    }

    scope.insert(enumElIdentifier, make_shared<LiteralNode>(ASTNode::NUMBER_LITERAL, to_string(i), nullptr));
  }

  // Insert the enum identifier itself into scope so we can remap types
  scope.insert(baseIdentifier, make_shared<TypeDeclarationNode>(DataTypes::NUMBER, nullptr));
}

void LiteralInlinerPass::remapEnumTypeReferences(shared_ptr<ASTNode> &ast) {
  shared_ptr<TypeDeclarationNode> typeDef = dynamic_pointer_cast<TypeDeclarationNode>(ast);
  
  shared_ptr<ASTNode> remappedType = lookupInScope(typeDef->getType());

  if (!remappedType) return;

  shared_ptr<TypeDeclarationNode> remappedTypeDecl = dynamic_pointer_cast<TypeDeclarationNode>(remappedType);

  typeDef->setType(remappedTypeDecl->getType());
}

bool LiteralInlinerPass::isLiteralAssignment(shared_ptr<ASTNode> ast) {
  if (ast->getNodeType() != ASTNode::ASSIGNMENT) return false;

  string identifierType = dynamic_pointer_cast<TypeDeclarationNode>(ast->getLeft()->getValue())->getType();

  return (
    (ast->getRight()->getNodeType() == ASTNode::BOOLEAN_LITERAL && identifierType == DataTypes::BOOLEAN) ||
    (ast->getRight()->getNodeType() == ASTNode::NUMBER_LITERAL && identifierType == DataTypes::NUMBER) ||
    (ast->getRight()->getNodeType() == ASTNode::STRING_LITERAL && identifierType == DataTypes::STRING)
  );
}
