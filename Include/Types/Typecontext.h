#pragma once
#ifndef TYPECONTEXT_H_
#define TYPECONTEXT_H_

#include <string>
#include <memory>
#include <unordered_map>
#include <string_view>
#include "llvm/IR/LLVMContext.h"
// This file will generate the type context as the name implies
namespace MemoLang {
    namespace {
        // add here when needed
        // Generic key to build a "string key" for a generic type
        std::string GenericKey(std::string_view ctor, const std::vector<const Type*>& args) { 
            std::string key = "G:"; 
            key += ctor; // adds the generic name "ctor"
            key += '<'; // connector
            for (std::size_t i = 0; i < /* length */args.size() ; ++i) 
            {
                if (i != 0) {
                     key += ',';
                    }
                key += args[i] == nullptr ? "?" : args[i]->display();
            }
            key += '>'; // deconnector
            return key;
        }
        
        
            



        }




        class Typecontext {
            public:
            


            private:


        }

    }

}







#endif // typecontext.h