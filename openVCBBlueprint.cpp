#include "openVCB.h"
#include "base64.h"
#include "SHA1.h"

namespace openVCB {    
    std::string removeWhitespace(std::string str) {
        str.erase(remove_if(str.begin(), str.end(), std::isspace), str.end());
        return str;
    }

    int readInt(std::vector<unsigned char>::iterator vector, int offset) {
        unsigned char* data = vector._Ptr;
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

        //SHA1 checksum
        SHA1 sha1;
        sha1.update(std::string(clipboardData.substr(12)));
        std::string calculatedChecksum = sha1.final();

        std::ostringstream result;
        for (size_t i = 0; i < 6; i++) {
            result << std::hex << std::setfill('0') << std::setw(2);
            result << (int)parsedData[i + 3];
        }

        if (calculatedChecksum.compare(0, 12, result.str())) {
            return false;
        }       

        //process header
        auto iterator = parsedData.begin();
        int width = readInt(iterator, 9);
        int height = readInt(iterator, 13);
        iterator += 17; //skip header

        //process layers
        while (iterator < parsedData.end())
        {
            size_t compressedSize = readInt(iterator, 0);
            int layerId = readInt(iterator, 4);
            int imgDSize = readInt(iterator, 8);
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
