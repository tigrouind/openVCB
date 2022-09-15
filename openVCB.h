#pragma once
/*
* This is the primary header ifle for openVCB
*/


#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>

// Enable multithreading
// #define OVCB_MT

#ifdef OVCB_MT
#include <atomic>
#endif

/// <summary>
/// Primary namespace for openVCB
/// </summary>
namespace openVCB {
	struct LatchInterface {
		glm::ivec2 pos;
		glm::ivec2 stride;
		glm::ivec2 size;
		int numBits;
		int gids[64];
	};

	/// <summary>
	/// 8 bit state.
	/// 7 bit type + 1 active bit
	/// 
	/// Add potential ink types here. 
	/// It is VERY important to keep components in pairs for on and off to
	/// to make sure the values are aligned correctly. 
	/// </summary>
	enum class Ink {
		None = 0,

		TraceOff = 1,
		ReadOff,
		WriteOff,
		Cross,

		BufferOff,
		OrOff,
		NandOff,

		NotOff,
		NorOff,
		AndOff,

		XorOff,
		XnorOff,

		ClockOff,
		LatchOff,
		LedOff,
		BundleOff,

		Filler,
		Annotation,

		numTypes,

		Trace = 129,
		Read,
		Write,
		InvalidCross,

		Buffer,
		Or,
		Nand,

		Not,
		Nor,
		And,

		Xor,
		Xnor,

		Clock,
		Latch,
		Led,
		Bundle,

		InvalidFiller,
		InvalidAnnotation
	};

	extern const int colorPallet[];
	extern const char* inkNames[];

	// Sets the ink type to be on or off
	inline Ink setOn(Ink ink, bool state) {
		return (Ink)(((int)ink & 0x7f) | (state << 7));
	}

	// Sets the ink type to be on
	inline Ink setOn(Ink ink) {
		return (Ink)(((int)ink & 0x7f) | 128);
	}

	// Sets the ink type to be off
	inline Ink setOff(Ink ink) {
		return (Ink)((int)ink & 0x7f);
	}

	// Gets the ink active state
	inline bool getOn(Ink ink) {
		return (int)ink >> 7;
	}

	// Gets the string name of the ink
	const char* getInkString(Ink ink);

	struct InkState {
#ifdef OVCB_MT
		// Number of active inputs
		std::atomic<int16_t> activeInputs;

		// Flags for traversal
		std::atomic<unsigned char> visited;
#else
		// Number of active inputs
		int16_t activeInputs;
		// Flags for traversal
		unsigned char visited;
#endif

		// Current ink
		unsigned char ink;
	};

	struct SparseMat {
		// Size of the matrix
		int n;
		// Number of non-zero entries
		int nnz;
		// CSC sparse matrix ptr
		int* ptr;
		// CSC sparse matrix rows
		int* rows;
	};

	// This represents a pixel with meta data as well as simulation ink type
	// Ths is for inks that have variants which do not affect simulation
	// i.e. Colored traces. 
	struct InkPixel {
		int16_t ink;
		int16_t meta;

		Ink getInk() {
			return (Ink)ink;
		}
	};

	class Project {
	public:
		int* vmem = nullptr; // null if vmem is not used
		size_t vmemSize = 0;
		std::string assembly;
		LatchInterface vmAddr;
		LatchInterface vmData;
		int lastVMemAddr = 0;

		~Project();

		int height;
		int width;

		// An image containing component indices
		unsigned char* originalImage = nullptr;
		InkPixel* image = nullptr;
		int* indexImage = nullptr;
		int* decoration[3]{ nullptr, nullptr, nullptr }; // on / off / unknown
		int ledPalette[16]{
			0x323841, 0xffffff, 0xff0000, 0x00ff00, 0x0000ff, 0xff0000, 0x00ff00, 0x0000ff,
			0xff0000, 0x00ff00, 0x0000ff, 0xff0000, 0x00ff00, 0x0000ff, 0xff0000, 0x00ff00
		};
		int numGroups;

		// Adjacentcy matrix
		// By default, the indices from ink groups first and then component groups
		SparseMat writeMap = {};
		InkState* states = nullptr;

		// Map of symbols during assembleVmem()
		std::unordered_map<std::string, long long> assemblySymbols;

		// Event queue
		int* updateQ[2]{ nullptr, nullptr };
		int16_t* lastActiveInputs = nullptr;

#ifdef OVCB_MT
		std::atomic<int> qSize;
#else
		int qSize;
#endif

		// Builds a project from an image. Remember to configure VMem
		void readFromVCB(std::string p);

		// Decode base64 data from clipboard, then process logic data
		bool readFromBlueprint(std::string clipboardData);

		// Parse VCB header
		void parseHeader(const std::vector<unsigned char>& logicData, int headerSize, unsigned char*& compressedData, size_t& compressedSize, int& width, int& height, int& imgDSize);

		// Decompress zstd logic data to an image
		bool processLogicData(unsigned char* compressedData, size_t compressedSize, int width, int height, int imgDSize);

		// Decompress zstd decoration data to an image
		bool processDecorationData(unsigned char* compressedData, size_t compressedSize, int width, int height, int imgDSize, int*& decoData);
		
		// Samples the ink at a pixel. Returns ink and group id
		std::pair<Ink, int> sample(glm::ivec2 pos);

		// Assemble the vmem from this->assembly
		void assembleVmem(char* err = nullptr);

		// Dump vmem contents to file
		void dumpVMemToText(std::string p);

		// Toggles the latch at position. 
		// Does nothing if it's not a latch
		void toggleLatch(glm::ivec2 pos);

		// Preprocesses the image into the simulation format
		void preprocess(bool useGorder = false);

		// Advances the simulation by n ticks
		int tick(int numTicks = 1, long long maxEvents = 0x7fffffffffffffffll);

		// Emits an event if it is not yet in the queue
		inline bool tryEmit(int gid) {
#ifdef OVCB_MT
			unsigned char expect = 0;
			// Check if this event is already in queue
			if (states[gid].visited.compare_exchange_strong(expect, 1, std::memory_order_relaxed)) {
				updateQ[1][qSize.fetch_add(1, std::memory_order_relaxed)] = gid;
				return true;
			}
			return false;
#else
			// Check if this event is already in queue
			if (states[gid].visited) return false;
			states[gid].visited = 1;
			updateQ[1][qSize++] = gid;
			return true;
#endif
		}

	private:
		bool readFromBlueprintV1(std::string clipboardData);
		bool readFromBlueprintV2(std::string clipboardData);
	};

}