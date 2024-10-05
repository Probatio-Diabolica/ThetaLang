#pragma once

#include <memory>
#include <string>
#include <sstream>
#include "ASTNode.hpp"

using namespace std;

namespace Theta {
  class CapsuleNode : public ASTNode {
  public:
    string name;

    CapsuleNode(string n, shared_ptr<ASTNode> parent) : ASTNode(ASTNode::CAPSULE, parent), name(n) {};

    string getName() { return name; }

    bool hasOwnScope() override { return true; }

    string toJSON() const override {
      ostringstream oss;

      oss << "{";
      oss << "\"type\": \"" << getNodeTypePretty() << "\"";
      oss << ", \"name\": \"" << name << "\"";
      oss << ", \"value\": " << (value ? value->toJSON() : "null");
      oss << "}";

      return oss.str();
    }
  };
}
