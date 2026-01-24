// SPDX-License-Identifier: Apache 2.0

#include "../include/mtlx-simple-parser.hh"
#include <cstring>

namespace tinyusdz {
namespace mtlx {

bool SimpleXMLParser::Parse(const std::string& xml) {
  XMLTokenizer tokenizer;
  
  if (!tokenizer.Initialize(xml.c_str(), xml.size())) {
    error_ = "Failed to initialize tokenizer: " + tokenizer.GetError();
    return false;
  }
  
  std::stack<SimpleXMLNodePtr> node_stack;
  SimpleXMLNodePtr current_node;
  Token token;
  
  while (tokenizer.NextToken(token)) {
    switch (token.type) {
      case TokenType::ProcessingInstruction:
        // Skip XML declaration
        continue;
        
      case TokenType::StartTag: {
        auto new_node = std::make_shared<SimpleXMLNode>(token.name);
        
        // Collect attributes
        Token attr_token;
        while (tokenizer.NextToken(attr_token)) {
          if (attr_token.type == TokenType::Attribute) {
            new_node->attributes[attr_token.name] = attr_token.value;
          } else if (attr_token.type == TokenType::SelfClosingTag) {
            // Self-closing tag, add to parent and continue
            if (!node_stack.empty()) {
              node_stack.top()->children.push_back(new_node);
            } else if (!root_) {
              root_ = new_node;
            }
            break;
          } else {
            // End of attributes, rewind this token
            // Since we can't rewind, we'll handle it in the next iteration
            // by checking if we have a pending token
            
            // For now, assume end of attributes
            break;
          }
        }
        
        // If not self-closing, push to stack
        if (attr_token.type != TokenType::SelfClosingTag) {
          if (!node_stack.empty()) {
            node_stack.top()->children.push_back(new_node);
          } else if (!root_) {
            root_ = new_node;
          }
          node_stack.push(new_node);
        }
        break;
      }
      
      case TokenType::EndTag: {
        if (node_stack.empty()) {
          error_ = "Unexpected end tag: " + token.name;
          return false;
        }
        
        if (node_stack.top()->name != token.name) {
          error_ = "Mismatched end tag: expected </" + node_stack.top()->name + 
                  "> but got </" + token.name + ">";
          return false;
        }
        
        node_stack.pop();
        break;
      }
      
      case TokenType::Text:
      case TokenType::CDATA: {
        if (!node_stack.empty()) {
          // Append text to current node
          node_stack.top()->text += token.value;
        }
        break;
      }
      
      case TokenType::Comment:
        // Ignore comments
        break;
        
      case TokenType::EndOfDocument:
        if (!node_stack.empty()) {
          error_ = "Unclosed tags at end of document";
          return false;
        }
        return true;
        
      case TokenType::Error:
        error_ = "Tokenizer error: " + tokenizer.GetError();
        return false;
        
      default:
        break;
    }
  }
  
  if (!node_stack.empty()) {
    error_ = "Unclosed tags at end of document";
    return false;
  }
  
  return root_ != nullptr;
}

} // namespace mtlx
} // namespace tinyusdz