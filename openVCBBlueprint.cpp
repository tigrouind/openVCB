#include "openVCB.h"

namespace openVCB {
    static const int B64index[256] = { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 62, 63, 62, 62, 63, 52, 53, 54, 55,
    56, 57, 58, 59, 60, 61,  0,  0,  0,  0,  0,  0,  0,  0,  1,  2,  3,  4,  5,  6,
    7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  0,
    0,  0,  0, 63,  0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51 };

    std::vector<unsigned char> b64decode(std::string data) {
        const size_t len = data.length();
        int pad = len > 0 && (len % 4 || data[len - 1] == '=');
        
        const size_t size = ((len + 3) / 4 - pad) * 4;
        std::vector<unsigned char> result(size / 4 * 3 + pad);

        for (size_t i = 0, j = 0; i < size; i += 4) {
            int n = B64index[data[i]] << 18 | B64index[data[i + 1]] << 12 | B64index[data[i + 2]] << 6 | B64index[data[i + 3]];
            result[j++] = n >> 16;
            result[j++] = n >> 8 & 0xFF;
            result[j++] = n & 0xFF;
        }

        if (pad) {
            int n = B64index[data[size]] << 18 | B64index[data[size + 1]] << 12;
            result[result.size() - 1] = n >> 16;

            if (len > size + 2 && data[size + 2] != '=') {
                n |= B64index[data[size + 2]] << 6;
                result.push_back(n >> 8 & 0xFF);
            }
        }
        return result;
    }

    bool isBase64(std::string text) {
        const size_t len = text.length();
		
		//must be multiple of 4
        if (len % 4 != 0) { 
            return false;
        }
		
		//valid characters only
        for (size_t i = 0; i < len; i ++) {
            char ch = text[i];
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '/' || ch == '+' || (i >= len - 3 && ch == '='))) {
                return false;
            }
        }

        return true;
    }

    std::string removeWhitespace(std::string str) {
        str.erase(remove_if(str.begin(), str.end(), std::isspace), str.end());
        return str;
    }

    int readInt(unsigned char* data, int offset) {        
        return data[offset] << 24 | data[offset + 1] << 16 | data[offset + 2] << 8 | data[offset + 3]; //big-endian
    }

    bool Project::readFromBlueprint(std::string clipboardData) {
        clipboardData = removeWhitespace(clipboardData);

        if (clipboardData.substr(0, 4) == "VCB+") {
            return readFromBlueprintV2(clipboardData);
        }
        else {
            return readFromBlueprintV1(clipboardData);
        }

        return false;
    }

    bool Project::readFromBlueprintV1(std::string clipboardData) {
        //original blueprint format
        if (!isBase64(clipboardData)) {
            return false;
        }

        std::vector<unsigned char> parsedData = b64decode(clipboardData);

        //check minimum size: zstd magic number (4) + vcb header (32)
        if (parsedData.size() <= 36) {
            return false;
        }

        //check zstd magic number
        if ((*(unsigned int*)&parsedData[0]) != 0xFD2FB528) {
            return false;
        }

        int imgDSize;
        unsigned char* cc;
        size_t ccSize;
        Project::parseHeader(parsedData, 32, cc, ccSize, width, height, imgDSize);
        return Project::processLogicData(cc, ccSize, width, height, imgDSize);
    }

    bool Project::readFromBlueprintV2(std::string clipboardData) {
        //new blueprint format
        clipboardData = clipboardData.substr(4);
        if (!isBase64(clipboardData)) {
            return false;
        }

        std::vector<unsigned char> parsedData = b64decode(clipboardData);
        if (parsedData.size() <= 17) { //header size (3+6+4+4)
            return false;
        }

        //process header
        auto iterator = parsedData.begin();
        int width = readInt(iterator._Ptr, 9);
        int height = readInt(iterator._Ptr, 13);
        iterator += 17; //skip header

        //process layers
        while (iterator < parsedData.end())
        {
            size_t compressedSize = readInt(iterator._Ptr, 0);
            int layerId = readInt(iterator._Ptr, 4);
            int imgDSize = readInt(iterator._Ptr, 8);
            unsigned char* compressedData = iterator._Ptr + 12;

            switch (layerId) {
            case 0: //logic
                if (!Project::processLogicData(compressedData, compressedSize, width, height, imgDSize)) {
                    return false;
                }
                break;
            case 1: //deco on
            case 2: //deco off
                if (!Project::processDecorationData(compressedData, compressedSize, width, height, imgDSize, decoration[layerId - 1])) {
                    return false;
                }
                break;
            }

            iterator += compressedSize; //next layer
        }

        return true;
    }
}
