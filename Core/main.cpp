#include "stdafx.h"
#include <iostream>
#include <fstream>
#include <deque>
#include <cstring>
#include <algorithm>
#include "datadefs.h"
#include "macros.h"
#include "wires.h"
#include "chipsim.h"
#include "ramedit.h"
#include "logger.h"
#include <assert.h>

vector<string> logVars;
std::deque<int32_t> recentLog;
string chipState;

const int spriteRamSize = 0x120;
const int paletteRamSize = 0x20;

enum class MemoryType
{
	CpuRam = 0, //$0000-$07FF (mirrored to $1FFF)
	PrgRam = 1, //$8000-$FFFF
	ChrRam = 2, //$0000-$1FFF
	NametableRam = 3, //$2000-$2FFF ($2000-$23FF is nametable A, $2400-$27FF is nametable B)
	PaletteRam = 4, //internal to the PPU - 32 bytes (including some mirrors)
	SpriteRam = 5,  //interal to the PPU.  256 bytes for primary + 32 bytes for secondary
	FullState = 6, //all of the above put together + a state of all of the nodes in the simulation (used for save/load state)
};

struct EmulationState
{
	int32_t halfcycle;
	int32_t hpos;
	int32_t vpos;
	int32_t vramaddr_v;
	int32_t vramaddr_t;
	int32_t vbl_flag;
	int32_t spr_overflow;
	int32_t spr0_hit;
	int32_t clk;
	int32_t ab;
	int32_t d;

	int32_t a;
	int32_t x;
	int32_t y;
	int32_t ps;
	int32_t sp;
	int32_t pc;
	int32_t opCode;
	int32_t fetch;

	int32_t cpu_ab;
	int32_t cpu_db;

	int32_t recentLog[10000];
};

DllExport void initEmulator()
{
	loadDataDefinitions();
	initDataStructures();
}

DllExport void release()
{
	stopLogging();
}

DllExport void reset(char* state, bool softReset)
{
	recentLog.clear();
	initChip(state, softReset);		
}

DllExport const char* getSaveState()
{
	chipState = getStateString();
	return chipState.c_str();
}

DllExport int32_t getMemoryState(MemoryType memoryType, uint8_t *buffer)
{
	switch(memoryType) {
		case MemoryType::CpuRam:
			memcpy(buffer, cpuRam, sizeof(cpuRam));
			return sizeof(cpuRam);

		case MemoryType::PrgRam:
			memcpy(buffer, prgRam, sizeof(prgRam));
			return sizeof(prgRam);

		case MemoryType::ChrRam: 
			memcpy(buffer, chrRam, sizeof(chrRam));
			return sizeof(chrRam);

		case MemoryType::NametableRam:
			memcpy(buffer, nametableRam, sizeof(nametableRam));
			return sizeof(nametableRam);

		case MemoryType::PaletteRam:
			for(int i = 0; i < paletteRamSize; i++) {
				buffer[i] = palette_read(i);
			}
			return paletteRamSize;

		case MemoryType::SpriteRam:
			for(int i = 0; i < spriteRamSize; i++) {
				buffer[i] = sprite_read(i);
			}
			return spriteRamSize;

		case MemoryType::FullState:
			string state = getSaveState();
			memcpy(buffer, state.c_str(), state.size());
			size_t pos = state.size();
			pos += getMemoryState(MemoryType::CpuRam, buffer + pos);
			pos += getMemoryState(MemoryType::PrgRam, buffer + pos);
			pos += getMemoryState(MemoryType::ChrRam, buffer + pos);
			pos += getMemoryState(MemoryType::NametableRam, buffer + pos);
			pos += getMemoryState(MemoryType::PaletteRam, buffer + pos);
			pos += getMemoryState(MemoryType::SpriteRam, buffer + pos);
			return (int32_t)pos;
	}

	return 0;
}

DllExport void setMemoryState(MemoryType memoryType, uint8_t *buffer, int32_t length)
{
	switch(memoryType) {
		case MemoryType::CpuRam: 
			memcpy(cpuRam, buffer, std::min(length, (int32_t)sizeof(cpuRam))); 
			break;

		case MemoryType::PrgRam:
			memcpy(prgRam, buffer, std::min(length, (int32_t)sizeof(prgRam)));
			break;

		case MemoryType::ChrRam:
			memcpy(chrRam, buffer, std::min(length, (int32_t)sizeof(chrRam)));
			break;

		case MemoryType::NametableRam:
			memcpy(nametableRam, buffer, std::min(length, (int32_t)sizeof(nametableRam)));
			break;

		case MemoryType::PaletteRam:
			for(int i = 0; i < paletteRamSize && i < length; i++) {
				palette_write(i, buffer[i]);
			}
			break;

		case MemoryType::SpriteRam:
			for(int i = 0; i < spriteRamSize && i < length; i++) {
				sprite_write(i, buffer[i]);
			}
			break;

		case MemoryType::FullState:
			size_t stateSize = getStateString().size();
			if(length >= (int32_t)(stateSize + sizeof(cpuRam) + sizeof(prgRam) + sizeof(chrRam) + sizeof(nametableRam) + paletteRamSize + spriteRamSize)) {
				char* state = new char[stateSize + 1];
				memcpy(state, buffer, stateSize);
				state[stateSize] = 0;
				initChip(state, false);
				buffer += stateSize;
				setMemoryState(MemoryType::CpuRam, buffer, sizeof(cpuRam)); buffer += sizeof(cpuRam);
				setMemoryState(MemoryType::PrgRam, buffer, sizeof(prgRam)); buffer += sizeof(prgRam);
				setMemoryState(MemoryType::ChrRam, buffer, sizeof(chrRam)); buffer += sizeof(chrRam);
				setMemoryState(MemoryType::NametableRam, buffer, sizeof(nametableRam)); buffer += sizeof(nametableRam);
				setMemoryState(MemoryType::PaletteRam, buffer, paletteRamSize); buffer += paletteRamSize;
				setMemoryState(MemoryType::SpriteRam, buffer, spriteRamSize); buffer += spriteRamSize;
			}
			break;
	}
}

DllExport void setTrace(char* nodeName)
{
	logVars.clear();
	for(string node : split(nodeName, '|')) {
		logVars.push_back(node);
	}
	recentLog.clear();
}

DllExport void getState(EmulationState *state)
{
	state->halfcycle = cycle;
	state->hpos = readBits("hpos");
	state->vpos = readBits("vpos");
	state->vramaddr_v = readBits("vramaddr_v");
	state->vramaddr_t = readBits("vramaddr_t");
	state->vbl_flag = readBit("vbl_flag");
	state->spr_overflow = readBit("spr_overflow");
	state->spr0_hit = readBit("spr0_hit");
	state->clk = readBit("clk0");
	state->ab = readBits("ab");
	state->d = readPpuDataBus();

	state->a = readBits("cpu_a");
	state->x = readBits("cpu_x");
	state->y = readBits("cpu_y");
	state->ps = readBits("cpu_p");
	state->sp = readBits("cpu_s");
	state->pc = (readBits("cpu_pch") << 8) | readBits("cpu_pcl");
	state->cpu_ab = readCpuAddressBus();
	state->cpu_db = readCpuDataBus();

	state->opCode = readBits("cpu_ir");
	state->fetch = isNodeHigh(nodenames["cpu_sync"]) ? readCpuDataBus() : -1;

	int i;
	int k = 0;
	for(i = (int)(recentLog.size() - logVars.size()); i >= 0; i -= (int)logVars.size()) {
		for(size_t j = 0; j < logVars.size(); j++) {
			state->recentLog[k] = recentLog[i+j];
			k++;
		}
	}
	if(k < 10000) {
		state->recentLog[k] = -1;
	} else {
		state->recentLog[9999] = -1;
	}
}

DllExport void step(uint32_t halfCycleCount)
{
	vector<int> logBuffer;
	for(uint32_t i = 0; i < halfCycleCount; i++) {
		step();

		for(size_t j = 0; j < logVars.size(); j++) {
			int value = readBits(logVars[j], 0);
			logBuffer.push_back(value);

			recentLog.push_back(value);
			if(recentLog.size() > 10000) {
				recentLog.pop_front();
			}
		}

		if(i % 10000 == 0) {
			flushLog(logVars, logBuffer);
			logBuffer.clear();
		}
	}
	flushLog(logVars, logBuffer);
}

void loadRom() {
	std::streampos size;
	char * memblock;

	ifstream file("C:\\Users\\bgour\\Downloads\\scanline.nes", ios::in | ios::binary | ios::ate);
	if (file.is_open())
	{
		size = file.tellg();
		memblock = new char[size];
		file.seekg(0, ios::beg);
		file.read(memblock, size);
		file.close();

		std::cout << "Loaded Rom" << std::endl;
		
		std::cout << "Mirroring Type: ";
		MirroringType mirroringType;

		if ((memblock[6] & 0x08) == 0x08) {
			std::cout << "Four Screens" << std::endl;
			mirroringType = MirroringType::FourScreens;
		}
		else if ((memblock[6] & 0x01) == 0x01) {
			std::cout << "Vertical" << std::endl;
			mirroringType = MirroringType::Vertical;
		}
		else {
			std::cout << "Horizontal" << std::endl;
			mirroringType = MirroringType::Horizontal;
		}
		assert(memblock[4] == 1 || memblock[4] == 2);
		int prgRamSize = memblock[4] * 0x4000;
		int chrRamSize = memblock[5] * 0x2000;

		std::cout << "PRG SIZE: " << prgRamSize << std::endl;
		std::cout << "CHR SIZE: " << chrRamSize << std::endl;

		char * prgRam = new char[0x8000];
		char * chrRam = new char[chrRamSize];

		std::copy(&memblock[16], &memblock[prgRamSize], prgRam);
		std::copy(&memblock[16 + prgRamSize], &memblock[16 + prgRamSize + chrRamSize], chrRam);

		if (prgRamSize == 0x4000) {
			std::copy(&memblock[16], &memblock[prgRamSize], &prgRam[0x4000]);
		}
		delete[] memblock;
		setMemoryState(MemoryType::ChrRam, (uint8_t *)chrRam, chrRamSize);
		setMemoryState(MemoryType::PrgRam, (uint8_t *)prgRam, 0x8000);
	} else {
		std::cout << "Unable to open file" << std::endl;
	}
}

void writeNodeState(std::ofstream &file) {
	int bytesWritten = 0;
	uint8_t data = 0;
	int nodenum = 0;
	for (node& node : nodes) {
		int startbit = (nodenum % 2) * 4;
		data = node.floating << startbit;
		data |= node.pulldown << (startbit + 1);
		data |= node.pullup << (startbit + 2);
		data |= node.state << (startbit + 3);
		nodenum++;
		if (nodenum % 2 == 0) {
			file.write(reinterpret_cast<const char *>(&data), sizeof(data));
			bytesWritten++;
			data = 0;
		}
	}

	if (nodenum % 2 != 0) {
		file.write(reinterpret_cast<const char *>(&data), sizeof(data));
		bytesWritten++;
	}

	std::cout << "Node Bytes Written: " << bytesWritten << std::endl;
}

void writeTransistorState(std::ofstream &file) {
	uint8_t data = 0;
	int transnum = 0;
	for (transistor& t : transistors) {
		data |= t.on << (transnum % 8);
		transnum++;

		if (transnum % 8 == 0) {
			file.write(reinterpret_cast<const char *>(&data), sizeof(data));
			data = 0;
		}
	}

	if (transnum % 8 != 0) {
		file.write(reinterpret_cast<const char *>(&data), sizeof(data));
	}
}

void writeRamState(std::ofstream &file) {
	file.write((char*)prgRam, sizeof(prgRam));
	file.write((char*)chrRam, sizeof(chrRam));
}

int main () { 
	std::cout << "Initializing..." << std::endl;
	initEmulator();
	std::cout << "floating: " << nodes[0].floating << std::endl;
	reset("", false);
	std::cout << "floating: " << nodes[0].floating << std::endl;
	std::cout << "Loading ROM..." << std::endl;
	loadRom();
	std::cout << "floating: " << nodes[0].floating << std::endl;
	int numSteps = 10;
	int halfCyclesPerStep = 1;
	std::ofstream file;
	file.open("C:\\Users\\bgour\\Desktop\\run.dat", std::ios_base::binary);
	file.write(reinterpret_cast<const char *>(&numSteps), sizeof(numSteps));
	file.write(reinterpret_cast<const char *>(&halfCyclesPerStep), sizeof(halfCyclesPerStep));
	writeRamState(file);
	writeNodeState(file);
	writeTransistorState(file);
	std::cout << "Running for 1000 half cycles..." << std::endl;
	for (int i = 0; i < numSteps; i++) {
		step(halfCyclesPerStep);
		writeRamState(file);
		writeNodeState(file);
		writeTransistorState(file);
		std::cout << "Executed " << (i + 1) * halfCyclesPerStep << " half cycles..." << std::endl;
	}
	
	file.close();
	std::cout << "Done!";
	std::cin.get();
	return 0;
}