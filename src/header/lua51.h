#pragma once

#include <cstddef>

namespace lua51 {
	struct lua_State;
	struct lua_Debug {
		int event;
		const char* name;
		const char* namewhat;
		const char* what;
		const char* source;
		int currentline;
		int nups;
		int linedefined;
		int lastlinedefined;
		char short_src[60];
		int i_ci;
	};
	using LuaCFunction = int(__cdecl*)(lua_State*);

	constexpr int registryIndex = -10000;
	constexpr int globalsIndex = -10002;
	constexpr int multiReturn = -1;
	constexpr int typeNone = -1;
	constexpr int typeNil = 0;
	constexpr int typeBoolean = 1;
	constexpr int typeNumber = 3;
	constexpr int typeString = 4;
	constexpr int typeTable = 5;
	constexpr int typeFunction = 6;

	bool init();
	bool ready();
	lua_State* getState();
	int getTop(lua_State* state);
	void setTop(lua_State* state, int index);
	void pop(lua_State* state, int count);
	void createTable(lua_State* state, int arrayCount, int fieldCount);
	void getField(lua_State* state, int index, const char* name);
	void setField(lua_State* state, int index, const char* name);
	void rawGetIndex(lua_State* state, int index, int item);
	void rawSetIndex(lua_State* state, int index, int item);
	int reference(lua_State* state);
	void unreference(lua_State* state, int reference);
	void getGlobal(lua_State* state, const char* name);
	void setGlobal(lua_State* state, const char* name);
	void pushNil(lua_State* state);
	void pushString(lua_State* state, const char* value);
	void pushNumber(lua_State* state, double value);
	void pushBoolean(lua_State* state, bool value);
	void pushFunction(lua_State* state, LuaCFunction function);
	void pushValue(lua_State* state, int index);
	int type(lua_State* state, int index);
	const char* toString(lua_State* state, int index, std::size_t* length = nullptr);
	double toNumber(lua_State* state, int index);
	bool toBoolean(lua_State* state, int index);
	LuaCFunction toFunction(lua_State* state, int index);
	int pcall(lua_State* state, int arguments, int results, int errorFunction);
	int fail(lua_State* state, const char* message);
	int loadBuffer(lua_State* state, const char* source, std::size_t size, const char* name, const char* mode);
	int loadString(lua_State* state, const char* source);
	int loadFile(lua_State* state, const char* filename);
	bool getStack(lua_State* state, int level, lua_Debug* debug);
	bool getInfo(lua_State* state, const char* information, lua_Debug* debug);
}
