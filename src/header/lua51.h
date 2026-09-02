#pragma once

#include <cstddef>

namespace lua51 {
	struct lua_State;
	using LuaCFunction = int(__cdecl*)(lua_State*);

	constexpr int globalsIndex = -10002;
	constexpr int typeNumber = 3;
	constexpr int typeString = 4;
	constexpr int typeTable = 5;
	constexpr int typeFunction = 6;

	bool init();
	bool ready();
	int getTop(lua_State* state);
	void setTop(lua_State* state, int index);
	void pop(lua_State* state, int count);
	void createTable(lua_State* state, int arrayCount, int fieldCount);
	void getField(lua_State* state, int index, const char* name);
	void setField(lua_State* state, int index, const char* name);
	void getGlobal(lua_State* state, const char* name);
	void setGlobal(lua_State* state, const char* name);
	void pushString(lua_State* state, const char* value);
	void pushNumber(lua_State* state, double value);
	void pushFunction(lua_State* state, LuaCFunction function);
	void pushValue(lua_State* state, int index);
	int type(lua_State* state, int index);
	const char* toString(lua_State* state, int index, std::size_t* length = nullptr);
	double toNumber(lua_State* state, int index);
	LuaCFunction toFunction(lua_State* state, int index);
	int pcall(lua_State* state, int arguments, int results, int errorFunction);
	int fail(lua_State* state, const char* message);
}
