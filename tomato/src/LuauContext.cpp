/*
 * File: /tomato/tomato/src/LuauContext.cpp
 * 
 * Created the 20 May 2024, 01:36 am by TinyMinori
 * Description :
 * 
 * Project repository: https://github.com/TinyMinori/tomato
 * Copyright 2024 TinyMinori
 */

#include "LuauContext.h"

#include <iostream>
#include <stdexcept>
#include <fstream>
#include <map>

void deleteStack(lua_State *state) {
    lua_close(state);
}

template<>
struct std::hash<std::any>
{
    std::size_t operator()(const std::any& s) const noexcept
    {
        std::size_t hash;
        if (s.type().name() == "int")
            hash = std::hash<int>{}(std::any_cast<int>(s));
        if (s.type().name() == "string")
            hash = std::hash<std::string>{}(std::any_cast<std::string>(s));
        if (s.type().name() == "bool")
            hash = std::hash<bool>{}(std::any_cast<bool>(s));
        return hash;
    }
};

namespace tomato {

    LuauContext::LuauContext(): p_L(luaL_newstate(), deleteStack) {
        if (p_L.get() == nullptr)
            throw std::bad_alloc();

        luaL_openlibs(p_L.get());
        luaL_sandbox(p_L.get());
        luaL_sandboxthread(p_L.get());
    }

    LuauContext::LuauContext(lua_State *threadState): p_L(threadState, deleteStack) {
        if (p_L.get() == nullptr)
            throw std::bad_alloc();
    }

    LuauContext::LuauContext(LuauContext&& other) : p_L(std::move(other.p_L)) { }

    LuauContext& LuauContext::operator=(LuauContext&& other) {
        if (this == &other)
            return *this;
        
        if (other.p_L.get() != nullptr)
            p_L = std::move(other.p_L);

        return *this;
    }

    void    LuauContext::load(const fs::path scriptPath) {
        // File checks and read
        if (!fs::is_regular_file(scriptPath))
            throw std::runtime_error(std::string("The script named ") + scriptPath.c_str() + " is not a correct script location.");

        std::ifstream   scriptFile(scriptPath);
        std::string     source;
        if (!scriptFile.is_open())
            throw std::runtime_error(std::string("Couldn't open file ") + scriptPath.c_str() + ".");
        
        std::string     line;
        while (scriptFile) {
            line = "";
            std::getline(scriptFile, line);
            source += line + '\n';
        }
        scriptFile.close();

        // Compile and load
        size_t  bytecodeSize = 0;
        char    *bytecode = luau_compile(source.c_str(), source.length(), NULL, &bytecodeSize);

        int result = luau_load(p_L.get(), scriptPath.c_str(), bytecode, bytecodeSize, 0);
        free(bytecode);

        if (result == LUA_ERRSYNTAX)
            throw std::runtime_error("Syntax error");
        if (result == LUA_ERRMEM)
            throw std::runtime_error("Memory error");
        if (result != LUA_OK)
            throw std::runtime_error("error #" + std::to_string(result));
    }

    int     LuauContext::call() {
        int result = lua_pcall(p_L.get(), 0, 0, 0);

        if (result == LUA_OK)
            return EXIT_SUCCESS;

        std::string errorMsg; 
        if (lua_type(p_L.get(), -1) == LUA_TSTRING) {
            size_t  strSize = 0;
            dumpstack();
            const char *msg = lua_tolstring(p_L.get(), -1, &strSize);
            errorMsg = std::string(msg, strSize);
        }

        if (errorMsg.empty())
            throw std::runtime_error("Unknown error message");

        if (result == LUA_ERRRUN)
            throw std::runtime_error(errorMsg);
        if (result == LUA_ERRMEM)
            throw std::out_of_range(errorMsg);
        if (result == LUA_ERRERR)
            throw std::logic_error(errorMsg);
        return result;
    }

    int     LuauContext::run(const fs::path scriptPath) {
        if (p_L.get() == nullptr)
            return STATE_NOT_INIALIZED;
        
        luaL_sandboxthread(p_L.get());
        load(scriptPath);
        return call();
    }
    
    bool    LuauContext::doesExist(const std::string &globalVarFunc) noexcept {
        lua_getglobal(p_L.get(), globalVarFunc.c_str());

        bool doesExist = (lua_type(p_L.get(), -1) != LUA_TNIL);

        lua_pop(p_L.get(), 1);
        return doesExist;
    }

    std::list<std::any> LuauContext::runFunction(const std::string &func, std::list<std::any> params) {
        luaL_sandboxthread(p_L.get());
        int stackSize = lua_gettop(p_L.get());
        lua_getglobal(p_L.get(), func.c_str());
        
        if (!lua_isfunction(p_L.get(), -1)) {
            std::cerr << "function '" << func.c_str() << "' does not exist." << std::endl;
            return std::list<std::any>();
        }

        std::size_t  paramsNumber = 0;
        for (auto arg: params) {
            auto argTypeName = std::string(arg.type().name());

            if (argTypeName == typeid(int).name())
                push(std::any_cast<int>(arg));
            else if (argTypeName == typeid(double).name())
                push(std::any_cast<double>(arg));
            else if (argTypeName == typeid(bool).name())
                push(std::any_cast<bool>(arg) ? 1 : 0);
            else if (argTypeName == typeid(std::string).name())
                push(std::any_cast<const char *>(arg));
            else if (argTypeName == typeid(char *).name())
                push(std::any_cast<const char *>(arg));
            else
                push();
            
            paramsNumber++;
        }

        int callResult = lua_pcall(p_L.get(), paramsNumber, LUA_MULTRET, 0);

        if (callResult != LUA_OK) {
            dumpstack();

            throw std::runtime_error(lua_tostring(p_L.get(), -1));
        }

        std::list<std::any> resultList = {};

        int resNbr = lua_gettop(p_L.get()) - stackSize;
        for (std::size_t i = 0; i < resNbr; i++) {
            std::any result;

            auto resultType = lua_type(p_L.get(), -1);
            if (resultType == LUA_TNONE)
                continue;
            
            switch (resultType) {
                case LUA_TNIL:
                    result = NULL;
                    break;
                case LUA_TBOOLEAN:
                    result = get<bool>(-1);
                    break;
                case LUA_TLIGHTUSERDATA:
                    result = get<LightUserData *>(-1);
                    break;
                case LUA_TUSERDATA:
                    result = get<UserData *>(-1);
                    break;
                case LUA_TNUMBER:
                    result = get<double>(-1);
                    break;
                case LUA_TSTRING:
                    result = get<char *>(-1);
                    break;
                case LUA_TFUNCTION:
                    result = get<lua_CFunction>(-1);
                    break;
                case LUA_TTHREAD:
                    result = get<lua_State*>(-1);
                    break;
                default:
                    std::cerr << "Missed a result" << std::endl;
            }

            resultList.push_back(result);
            lua_pop(p_L.get(), 1);
        }

        return resultList;
    }


    int LuauContext::getVariableType(const std::string &varName) {
        lua_getglobal(p_L.get(), varName.c_str());
        int type = getVarTypeInStack(-1);

        lua_pop(p_L.get(), 1);
        return type;
    }

    int LuauContext::getVarTypeInStack(StackIndex idx) {
        return lua_type(p_L.get(), idx);
    }

    std::any    LuauContext::getVariable(const std::string &varName) {
        lua_getglobal(p_L.get(), varName.c_str());

        std::any variable = getVarInStack(-1);

        lua_pop(p_L.get(), 1);
        return variable;
    }

    std::any    LuauContext::getVarInStack(StackIndex idx) {
        int type = lua_type(p_L.get(), idx);
        if (type == LUA_TNONE)
            return nullptr;

        std::any result;
        switch (type) {
            case LUA_TNIL:
                result = nullptr;
                break;
            case LUA_TBOOLEAN:
                result = get<bool>(idx);
                break;
            case LUA_TLIGHTUSERDATA:
                result = get<LightUserData *>(idx);
                break;
            case LUA_TUSERDATA:
                result = get<UserData *>(idx);
                break;
            case LUA_TNUMBER: {
                double first = get<double>(idx);
                int second = get<int>(idx);

                if (first == second)
                    result = second;
                else
                    result = first;
                break;
            }
            case LUA_TSTRING:
                result = get<char *>(idx);
                break;
            case LUA_TFUNCTION:
                result = get<lua_CFunction>(idx);
                break;
            case LUA_TTHREAD:
                result = get<lua_State*>(idx);
                break;
            case LUA_TTABLE: {
                int stackSizeStart = lua_gettop(p_L.get());
                int stackPosKey = stackSizeStart + 1;
                int stackPosValue = stackSizeStart + 2;
                push();
                std::map<KeyType, std::any> map;
                while (lua_next(p_L.get(), stackSizeStart) != 0) {
                    std::any key = getVarInStack(stackPosKey);
                    std::any value = getVarInStack(stackPosValue);

                    int keyType = lua_type(p_L.get(), stackPosKey);                    
                    if (keyType == LUA_TNUMBER)
                        map.insert(std::pair{ std::any_cast<int>(key), value });
                    else if (keyType == LUA_TSTRING)
                        map.insert(std::pair { std::any_cast<char *>(key), value });
                    else if (keyType == LUA_TBOOLEAN)
                        map.insert(std::pair { std::any_cast<bool>(key), value });
                    lua_pop(p_L.get(), 1);
                }
                result = map;
                break;
            }
            default:
                std::cerr << "Missed a result" << std::endl;
        }
        return result;
    }

    void  LuauContext::push() {
        lua_pushnil(p_L.get());
    }
    
    void    LuauContext::dumpstack() noexcept {
        std::clog << std::boolalpha;
        std::clog << "*************************" << std::endl;
        std::clog << "* Start of stack dump   *" << std::endl;
        int top = lua_gettop(p_L.get());

        if (top == 0)
            std::clog << "* Empty stack           *" << std::endl;
        
        for (int i = top; i > 0; i--) {
            std::clog << i << "\t" << std::setfill(' ') << std::setw(10) << luaL_typename(p_L.get(), i) << "\t";

            switch (lua_type(p_L.get(), i)) {
                case LUA_TNUMBER:
                    std::clog << get<double>(i) << std::endl;
                    break;
                case LUA_TSTRING:
                    std::clog << get<char*>(i) << std::endl;
                    break;
                case LUA_TBOOLEAN:
                    std::clog << get<bool>(i) << std::endl;
                    break;
                case LUA_TNIL:
                    std::clog << "nil" << std::endl;
                    break;
                default:
                    std::clog << get<void*>(i) << std::endl;
                    break;
            }
        }
        std::clog << "* End of stack dump     *" << std::endl;
        std::clog << "*************************" << std::endl;
    }
}