#include "shader.h"
#include "shader_recompiler.h"
#include "dxc_compiler.h"

#include <fstream>
#include <format>
#include <bit>
#include <string_view>

static std::unique_ptr<uint8_t[]> readAllBytes(const char* filePath, size_t& fileSize)
{
    FILE* file = fopen(filePath, "rb");
    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    auto data = std::make_unique<uint8_t[]>(fileSize);
    fread(data.get(), 1, fileSize, file);
    fclose(file);
    return data;
}

static void writeAllBytes(const char* filePath, const void* data, size_t dataSize)
{
    FILE* file = fopen(filePath, "wb");
    fwrite(data, 1, dataSize, file);
    fclose(file);
}

struct RecompiledShader
{
    uint8_t* data = nullptr;
    IDxcBlob* dxil = nullptr;
    std::vector<uint8_t> spirv;
    uint32_t specConstantsMask = 0;
};

int main(int argc, char** argv)
{
#ifndef XENOS_RECOMP_INPUT
    if (argc < 4)
    {
        printf("Usage: XenosRecomp [input path] [output path] [shader common header file path]");
        return 0;
    }
#endif

    const char* input =
#ifdef XENOS_RECOMP_INPUT 
        XENOS_RECOMP_INPUT
#else
        argv[1]
#endif
    ;

    const char* output =
#ifdef XENOS_RECOMP_OUTPUT 
        XENOS_RECOMP_OUTPUT
#else
        argv[2]
#endif
        ;
    
    const char* includeInput =
#ifdef XENOS_RECOMP_INCLUDE_INPUT
        XENOS_RECOMP_INCLUDE_INPUT
#else
        argv[3]
#endif
        ;

    size_t includeSize = 0;
    auto includeData = readAllBytes(includeInput, includeSize);
    std::string_view include(reinterpret_cast<const char*>(includeData.get()), includeSize);

    if (std::filesystem::is_directory(input))
    {
        std::vector<std::unique_ptr<uint8_t[]>> files;
        std::map<XXH64_hash_t, RecompiledShader> shaders;

        for (auto& file : std::filesystem::recursive_directory_iterator(input))
        {
            if (std::filesystem::is_directory(file))
            {
                continue;
            }
            
            size_t fileSize = 0;
            auto fileData = readAllBytes(file.path().string().c_str(), fileSize);
            bool foundAny = false;

            for (size_t i = 0; fileSize > sizeof(ShaderContainer) && i < fileSize - sizeof(ShaderContainer) - 1;)
            {
                auto shaderContainer = reinterpret_cast<const ShaderContainer*>(fileData.get() + i);
                size_t dataSize = shaderContainer->virtualSize + shaderContainer->physicalSize;

                if ((shaderContainer->flags & 0xFFFFFF00) == 0x102A1100 &&
                    dataSize <= (fileSize - i) &&
                    shaderContainer->field1C == 0 &&
                    shaderContainer->field20 == 0)
                {
                    XXH64_hash_t hash = XXH3_64bits(shaderContainer, dataSize);
                    auto shader = shaders.try_emplace(hash);
                    if (shader.second)
                    {
                        shader.first->second.data = fileData.get() + i;
                        foundAny = true;
                    }

                    i += dataSize;
                }
                else
                {
                    i += sizeof(uint32_t);
                }
            }

            if (foundAny)
                files.emplace_back(std::move(fileData));
        }

        std::atomic<uint32_t> progress = 0;

        //std::for_each(std::execution::par_unseq, shaders.begin(), shaders.end(), [&](auto& hashShaderPair)
        std::for_each(std::execution::seq, shaders.begin(), shaders.end(), [&](auto& hashShaderPair)
            {
                auto& shader = hashShaderPair.second;

#if 1
                {
                    bool isPixelShader = (std::byteswap(((uint32_t*)shader.data)[0]) & 0x1) == 0;

                    fmt::println("{:s}-Shader: {:s}{:016X}", isPixelShader ? "Pixel" : "Vertex", isPixelShader ? " " : "", hashShaderPair.first);

                    size_t virtualSize = std::byteswap(((uint32_t*)shader.data)[1]);
                    size_t physicalSize = std::byteswap(((uint32_t*)shader.data)[2]);
                    std::ofstream outShader(std::format("NG2/{:016X}.x{:s}u", hashShaderPair.first, isPixelShader ? "p" : "v"), std::ios::binary | std::ios::beg);
                    if (outShader) {
                        outShader.write((const char*)shader.data, virtualSize + physicalSize);
                    }
                }
#endif

                switch (hashShaderPair.first) {
                case 0x1710C6ED029F8F44: // float4Constants.find(reg); assert failed
                case 0x647AB8FD100B0C78: // float4Constants.find(reg); assert failed
                case 0x69DCC063DE116419: // float4Constants.find(reg); assert failed
                case 0xAA7DC775B419E369: // float4Constants.find(reg); assert failed
                case 0xAC5103896C41DFA2: // float4Constants.find(reg); assert failed
                case 0xBF775F60AFF1BF04: // float4Constants.find(reg); assert failed
                case 0xC01DF6FA1A1D0B03: // float4Constants.find(reg); assert failed
                case 0xC02B3363393DDFC7: // float4Constants.find(reg); assert failed
                case 0xDC99A49519396036: // float4Constants.find(reg); assert failed
                case 0xF835D6FA1678862C: // float4Constants.find(reg); assert failed
                    return;
                }

                thread_local ShaderRecompiler recompiler;
                recompiler = {};
                recompiler.recompile(shader.data, include);

#if 1
                {
                    writeAllBytes(std::format("NG2/{:016X}.{:s}.hlsl", hashShaderPair.first, recompiler.isPixelShader ? "ps" : "vs").c_str(), recompiler.out.data(), recompiler.out.size());
                }
#endif

                switch (hashShaderPair.first) {
                case 0x045A2DF2CED3EEE1:
                case 0x05CD85F032AC6298:
                case 0x078F67A72502BEAB:
                case 0x0938478F27EF54B9:
                case 0x0962B2E2998E325B:
                case 0x09B3BEA1AB24030F:
                case 0x0BBABEFC2EC0D864: // mixed up Texture1D/Texture2D
                case 0x0EE7221D2402018D: // mixed up Texture1D/Texture2D
                case 0x0FA2D990D83F72FA:
                case 0x111D4D67210D2C0B: // mixed up Texture1D/Texture2D
                case 0x1155509517693D95:
                case 0x11559A42F5EBAC63:
                case 0x120468B401196468:
                case 0x135B8C637C0BB0A8:
                case 0x13A20BDD59DF83E5:
                case 0x13CFE07EDB015E50:
                case 0x142E5238C0B03313: // mixed up Texture1D/Texture2D
                case 0x169DE8E0DE15D677: // mixed up Texture1D/Texture2D
                case 0x17E9F45522919327:
                case 0x18161E8508C7FB4D:
                case 0x19CBBDA647449AF2:
                case 0x1A2A5591BD7DFBD7:
                case 0x1BDBB740186EE193:
                case 0x1C3AD4D1CF871A49:
                case 0x1CAC2FAF8846CCE2:
                case 0x1DE51A0745C4F2B5: // mixed up Texture1D/Texture2D
                case 0x1E97990CF0F6CD58:
                case 0x1F008C01B43256E0:
                case 0x1F846A53260E5DD2:
                case 0x1F9537179C3B0C21: // mixed up Texture1D/Texture2D
                case 0x2189CB4EA0EFE1FE: // mixed up Texture1D/Texture2D
                case 0x2354AF10ABCA59A5:
                case 0x25074C221189614B: // mixed up Texture1D/Texture2D
                case 0x2920B5FD879CAF5F:
                case 0x2A444BB475FA412F:
                case 0x2AF155BD5D21A916:
                case 0x2C02C6EC2B719331: // mixed up Texture1D/Texture2D
                case 0x2C5B183DC2EE40A5:
                case 0x2E067716C126CA71:
                case 0x303A5074E0ABFB1D:
                case 0x3241ABBCEC96A87B:
                case 0x33FAFB4D6C09D751:
                case 0x3408B8EB67DEBC4B:
                case 0x345DB87010A1D9D0: // mixed up Texture1D/Texture2D
                case 0x34FD9D06B85D175B:
                case 0x356BAD195777C23E:
                case 0x367575C8561F3791:
                case 0x3AFCB227B68EC7C2:
                case 0x3F09E8FB2261D219:
                case 0x3F5249D53AE3A7F6:
                case 0x41B6866CD1F13232:
                case 0x41C0068BCF742999: // mixed up Texture1D/Texture2D
                case 0x435D488E85D46EC2:
                case 0x443828E89DBBDF6F:
                case 0x48376768621EC913:
                case 0x49A130B7C1BC5934:
                case 0x49C46A4C4DA05CC2:
                case 0x49EB75294474B9E2:
                case 0x4E7424327FE41A91:
                case 0x4E8567BD28B5F72B:
                case 0x4E878536D519EF9A:
                case 0x509CEE184D296779:
                case 0x50FBC464C050F4DC:
                case 0x52B44802C5388B4A: // mixed up Texture1D/Texture2D
                case 0x52CF25BB3FAF9B08:
                case 0x52F0933EC6783E0D:
                case 0x54E33573CA6E03D9:
                case 0x554BC4B2A0B42736:
                case 0x5556655D173E49EC: // mixed up Texture1D/Texture2D
                case 0x559283FB3ABC2CED:
                case 0x559FDEDD5246354B:
                case 0x578F64B4DBEE74E5:
                case 0x57F299E787F270C1:
                case 0x57F88A28E811EEE4: // mixed up Texture1D/Texture2D
                case 0x581ED6D97672EFE6:
                case 0x58AC48F4A583E9AA:
                case 0x5932403038D564D5:
                case 0x5A9B0E4F2970C177:
                case 0x5AA16C36C7CF89B2:
                case 0x5BC7E4DCA90F22B6:
                case 0x5C27540EA1A7CB9B:
                case 0x628D7474BADF8C87:
                case 0x62A3D13A92BF31C4: // mixed up Texture1D/Texture2D
                case 0x639BAB73C1444802:
                case 0x63DBA211F09CFA2E:
                case 0x64FD8ED09CBA1372:
                case 0x66080A54F8210D78: // mixed up Texture1D/Texture2D
                case 0x683D2523AEDF6E8E:
                case 0x688E42C8B3093E37:
                case 0x68E38870ED6CFDF8:
                case 0x6B714573250410C7:
                case 0x6C3C2DF6E79491ED: // mixed up Texture1D/Texture2D
                case 0x6CBAE493635DC1C3:
                case 0x6D04BBB09D4F38EF: // mixed up Texture1D/Texture2D
                case 0x6EE677140A603FCF:
                case 0x6F063B96CCE821FC:
                case 0x6F523A71025C4FD0:
                case 0x70117449452495AB:
                case 0x702046602E943E1B: // mixed up Texture1D/Texture2D
                case 0x72EBF8D0CEB4AA0A:
                case 0x731438F38B9C283A:
                case 0x736D6A13F0311AFD:
                case 0x738E449234D4F2EB:
                case 0x73DF66D1FD42E2EB: // mixed up Texture1D/Texture2D
                case 0x7574F5151DA29301:
                case 0x75E5B6BC8296887F:
                case 0x75FFB2E0459F79D7:
                case 0x76CCB4F45BA476DD:
                case 0x773CE59A94B90C17:
                case 0x776907C0BBFF3A2C:
                case 0x79BD1A1B034776C9:
                case 0x79F5B7BD83311FE9:
                case 0x7BAF6D0D0826725A:
                case 0x7BCD5E1DDCC90410:
                case 0x7C3BE79B22A2CA6B: // mixed up Texture1D/Texture2D
                case 0x7C86DAAD3FFCD623:
                case 0x7CC1A16FB1B40182:
                case 0x7CEA7AF80C510DE9: // mixed up Texture1D/Texture2D
                case 0x7D2EBADF1DF4B306:
                case 0x7D7B9634653FDB27:
                case 0x7DAB13E4781868B2:
                case 0x7FA9B7B31716993D:
                case 0x806C2E530E226E02:
                case 0x80AC83A34C9AFD9C:
                case 0x8282819E0F311055:
                case 0x82AF1948D5C96CBC: // mixed up Texture1D/Texture2D
                case 0x830189419C826BA7:
                case 0x83319195D2D62AF7:
                case 0x86AC24E46C4FC0FB:
                case 0x87AA533F4FD40365:
                case 0x8AD4A8215BEABEEF:
                case 0x8DF87AA797CED9B1:
                case 0x9179F44574C22100:
                case 0x928FE7BF0651686B:
                case 0x92C16EB9CB7DD2A7: // mixed up Texture1D/Texture2D
                case 0x9373E6B1B65C15FB:
                case 0x93C4B485E4319E2D:
                case 0x951E280D8B55F8D0:
                case 0x977B57E9FF44B8AE:
                case 0x9A8F21FADEB38CF3: // mixed up Texture1D/Texture2D
                case 0x9D065C321182E0A6: // mixed up Texture1D/Texture2D
                case 0x9D2F02C75730F92D:
                case 0x9EFCC41C7DD7BB50: // mixed up Texture1D/Texture2D
                case 0xA0F3F139BF4BC389: // mixed up Texture1D/Texture2D
                case 0xA12E4FEA17361D17:
                case 0xA13771DCDBCFD363:
                case 0xA1CE5A560A45C827:
                case 0xA1EF88ACF4104022:
                case 0xA2C073CDADB64D44:
                case 0xA58F7744E994FED1:
                case 0xA5AEB854BECA69B6:
                case 0xA741DEC9176E3A83:
                case 0xA7DF57D5437D5A10:
                case 0xA8A1C0D96C46D7E3:
                case 0xA94C83FBC3743503:
                case 0xA98595270CF214DC:
                case 0xAABE1E17B25A84E4:
                case 0xAB81740B1AB6455D:
                case 0xAF337DE559289D28:
                case 0xAFDEEF7A3EFC0620:
                case 0xB0701C75BA781BB5:
                case 0xB073FC879529A49D:
                case 0xB279702BACB71674:
                case 0xB6AD0121744E2E71:
                case 0xB765F4FAAE7C3FF1:
                case 0xB856595AD9C51546:
                case 0xB8FC497FCE0B524A: // mixed up Texture1D/Texture2D
                case 0xBB1AE4C8BC2DF361: // mixed up Texture1D/Texture2D
                case 0xBCBAF855B2BBC651:
                case 0xBF2CC30166ACDBC9: // mixed up Texture1D/Texture2D
                case 0xBF8BB382119E5F7F:
                case 0xC07D54EEAE590DAE:
                case 0xC0A01B9997DAC5C8:
                case 0xC22B93396C29BD13: // mixed up Texture1D/Texture2D
                case 0xC26E5035B12A26B2:
                case 0xC7A1B35709DA0B0A:
                case 0xC807B116EB40EDBE: // mixed up Texture1D/Texture2D
                case 0xC845A4701C54552E: // mixed up Texture1D/Texture2D
                case 0xCC09AF2DB1745E31:
                case 0xCC522350F2EB0BED:
                case 0xCE04153340FA4E3F:
                case 0xCEA0AB72295A2B3A: // mixed up Texture1D/Texture2D
                case 0xCED088DA39014B43:
                case 0xCF9DEF7451F09E23:
                case 0xD03A50E8A124940E:
                case 0xD1187EF6FC7FF738:
                case 0xD170C9C1CCB102C2:
                case 0xD1A61220C9A44A66:
                case 0xD24C4E1B1CB0F5F7:
                case 0xD38FBB0D643BC978:
                case 0xD4B67533B4E186D5:
                case 0xD4DE06F11BD4E801:
                case 0xD4E72E9C63C4BCB8:
                case 0xD4F3C6A38618E764: // mixed up Texture1D/Texture2D
                case 0xD5A62118D613373E:
                case 0xD6049719DBD30C0B:
                case 0xD6799679ACF95C19: // mixed up Texture1D/Texture2D
                case 0xD6F9B1672370B674:
                case 0xD703EAD1BE6FB8EA:
                case 0xD72BF694CF9FBCC7:
                case 0xD7A4920FB48702D3:
                case 0xD93D99A5E49E91C0:
                case 0xDA428C27D9B97CC8:
                case 0xDE905D96B872F4CC:
                case 0xDE94C8E2711927EA:
                case 0xDF8C97C61CB29C1E:
                case 0xE02911250569A354:
                case 0xE151174243AAD528:
                case 0xE4F753647C9DBA5A:
                case 0xE5F3E3BB6CE5549B: // mixed up Texture1D/Texture2D
                case 0xE60C75FBDF1E7E35: // mixed up Texture1D/Texture2D
                case 0xE61DCC467D9BAD8B:
                case 0xE70E22C9B3AD0486:
                case 0xE7F32F28486D4F88:
                case 0xE9B1CB6112E549E6: // mixed up Texture1D/Texture2D
                case 0xEBDF5919F55FD7C4:
                case 0xEFB376DD5CEE5C7C:
                case 0xEFFB41D2D1987D26:
                case 0xF03AFB3D67C655FB:
                case 0xF14B29F89722FF14:
                case 0xF21634F3E10D3543:
                case 0xF2840AC2DC659134:
                case 0xF307C88B9406A44B:
                case 0xF4F60C9FBA8CB1A3:
                case 0xF57A68A227173805: // mixed up Texture1D/Texture2D
                case 0xF5EEDE2558E2C004:
                case 0xF683DE6F16E8248E:
                case 0xF6E38857FF3C259D:
                case 0xF7138E7E630D3B21:
                case 0xF95824934693E427:
                case 0xFACC4E66C58D1841:
                case 0xFB8C2CFDFA0F01BF:
                case 0xFE2EA25D4252FEF2:
                case 0xFE83C87572FC8F64:
                    return;
                }

                shader.specConstantsMask = recompiler.specConstantsMask;

                thread_local DxcCompiler dxcCompiler;

#ifdef XENOS_RECOMP_DXIL
                shader.dxil = dxcCompiler.compile(recompiler.out, recompiler.isPixelShader, recompiler.specConstantsMask != 0, false);
                assert(shader.dxil != nullptr);
                assert(*(reinterpret_cast<uint32_t *>(shader.dxil->GetBufferPointer()) + 1) != 0 && "DXIL was not signed properly!");
#endif

                IDxcBlob* spirv = dxcCompiler.compile(recompiler.out, recompiler.isPixelShader, false, true);
                assert(spirv != nullptr);

                bool result = smolv::Encode(spirv->GetBufferPointer(), spirv->GetBufferSize(), shader.spirv, smolv::kEncodeFlagStripDebugInfo);
                assert(result);

                spirv->Release();

                size_t currentProgress = ++progress;
                if ((currentProgress % 10) == 0 || (currentProgress == shaders.size() - 1))
                    fmt::println("Recompiling shaders... {}%", currentProgress / float(shaders.size()) * 100.0f);
            });

        fmt::println("Creating shader cache...");

        StringBuffer f;
        f.println("#include \"shader_cache.h\"");
        f.println("ShaderCacheEntry g_shaderCacheEntries[] = {{");

        std::vector<uint8_t> dxil;
        std::vector<uint8_t> spirv;

        for (auto& [hash, shader] : shaders)
        {
            f.println("\t{{ 0x{:X}, {}, {}, {}, {}, {} }},",
                hash, dxil.size(), (shader.dxil != nullptr) ? shader.dxil->GetBufferSize() : 0, spirv.size(), shader.spirv.size(), shader.specConstantsMask);

            if (shader.dxil != nullptr)
            {
                dxil.insert(dxil.end(), reinterpret_cast<uint8_t *>(shader.dxil->GetBufferPointer()),
                    reinterpret_cast<uint8_t *>(shader.dxil->GetBufferPointer()) + shader.dxil->GetBufferSize());
            }
            
            spirv.insert(spirv.end(), shader.spirv.begin(), shader.spirv.end());
        }

        f.println("}};");

        fmt::println("Compressing DXIL cache...");

        int level = ZSTD_maxCLevel();

#ifdef XENOS_RECOMP_DXIL
        std::vector<uint8_t> dxilCompressed(ZSTD_compressBound(dxil.size()));
        dxilCompressed.resize(ZSTD_compress(dxilCompressed.data(), dxilCompressed.size(), dxil.data(), dxil.size(), level));

        f.print("const uint8_t g_compressedDxilCache[] = {{");

        for (auto data : dxilCompressed)
            f.print("{},", data);

        f.println("}};");
        f.println("const size_t g_dxilCacheCompressedSize = {};", dxilCompressed.size());
        f.println("const size_t g_dxilCacheDecompressedSize = {};", dxil.size());
#endif

        fmt::println("Compressing SPIRV cache...");

        std::vector<uint8_t> spirvCompressed(ZSTD_compressBound(spirv.size()));
        spirvCompressed.resize(ZSTD_compress(spirvCompressed.data(), spirvCompressed.size(), spirv.data(), spirv.size(), level));

        f.print("const uint8_t g_compressedSpirvCache[] = {{");

        for (auto data : spirvCompressed)
            f.print("{},", data);

        f.println("}};");

        f.println("const size_t g_spirvCacheCompressedSize = {};", spirvCompressed.size());
        f.println("const size_t g_spirvCacheDecompressedSize = {};", spirv.size());
        f.println("const size_t g_shaderCacheEntryCount = {};", shaders.size());

        writeAllBytes(output, f.out.data(), f.out.size());
    }
    else
    {
        ShaderRecompiler recompiler;
        size_t fileSize;
        recompiler.recompile(readAllBytes(input, fileSize).get(), include);
        writeAllBytes(output, recompiler.out.data(), recompiler.out.size());
    }

    return 0;
}
