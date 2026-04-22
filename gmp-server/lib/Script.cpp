#include "Script.h"

#include <spdlog/spdlog.h>

#include "sol/sol.hpp"
// Binds
#include "Lua/lua_constants.h"
#include "Lua/event_bind.h"
#include "Lua/function_bind.h"
#include "Lua/lua_json.h"
#include "Lua/lua_sky.h"
#include "Lua/lua_way.h"

using namespace std;

LuaScript::LuaScript() {
  BindDomainSpecific();
}

void LuaScript::BindDomainSpecific() {
  lua::bindings::BindEvents(lua_);
  lua::bindings::BindServerConstants(lua_);
  lua::bindings::BindFunctions(lua_, timer_manager_);
  lua::bindings::BindJson(lua_);
  lua::bindings::BindWay(lua_);
  lua::bindings::BindSky(lua_);
}