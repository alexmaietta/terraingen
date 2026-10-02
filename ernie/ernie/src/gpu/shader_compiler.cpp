#include "ernie/gpu/shader_compiler.h"

#include <slang-com-ptr.h>
#include <slang.h>

#include <cstring>
#include <mutex>
#include <stdexcept>

namespace ernie {

namespace {

std::string Text(slang::IBlob* blob) {
    return blob != nullptr ? std::string(static_cast<const char*>(blob->getBufferPointer()),
                                         blob->getBufferSize())
                           : std::string();
}

}  // namespace

struct ShaderCompiler::Impl {
    Slang::ComPtr<slang::IGlobalSession> global;
    mutable std::mutex mutex;

    Slang::ComPtr<slang::IBlob> Compile(std::span<const ShaderModuleSource> modules,
                                        const std::string& entryPoint, SlangCompileTarget format,
                                        const char* profile) const {
        if (modules.empty()) {
            throw std::invalid_argument("No shader modules to compile");
        }
        std::lock_guard lock(mutex);
        slang::TargetDesc target{};
        target.format = format;
        if (profile != nullptr) {
            target.profile = global->findProfile(profile);
        }
        slang::CompilerOptionEntry matrixLayout{};
        matrixLayout.name = slang::CompilerOptionName::MatrixLayoutColumn;
        matrixLayout.value.intValue0 = 1;
        slang::SessionDesc sessionDesc{};
        sessionDesc.targets = &target;
        sessionDesc.targetCount = 1;
        sessionDesc.compilerOptionEntries = &matrixLayout;
        sessionDesc.compilerOptionEntryCount = 1;
        Slang::ComPtr<slang::ISession> session;
        if (SLANG_FAILED(global->createSession(sessionDesc, session.writeRef()))) {
            throw std::runtime_error("Cannot create a Slang session");
        }

        slang::IModule* last = nullptr;
        for (const ShaderModuleSource& module : modules) {
            Slang::ComPtr<slang::IBlob> diagnostics;
            const std::string path = module.name + ".slang";
            last = session->loadModuleFromSourceString(
                module.name.c_str(), path.c_str(), module.source.c_str(), diagnostics.writeRef());
            if (last == nullptr) {
                throw std::invalid_argument("Shader module " + module.name +
                                            " failed to compile:\n" + Text(diagnostics));
            }
        }

        Slang::ComPtr<slang::IEntryPoint> entry;
        if (SLANG_FAILED(last->findEntryPointByName(entryPoint.c_str(), entry.writeRef()))) {
            throw std::invalid_argument("Shader module " + modules.back().name +
                                        " has no entry point " + entryPoint);
        }
        slang::IComponentType* parts[] = {last, entry};
        Slang::ComPtr<slang::IComponentType> program;
        Slang::ComPtr<slang::IBlob> diagnostics;
        if (SLANG_FAILED(session->createCompositeComponentType(parts, 2, program.writeRef(),
                                                               diagnostics.writeRef()))) {
            throw std::invalid_argument("Cannot compose shader program:\n" + Text(diagnostics));
        }
        Slang::ComPtr<slang::IComponentType> linked;
        if (SLANG_FAILED(program->link(linked.writeRef(), diagnostics.writeRef()))) {
            throw std::invalid_argument("Cannot link shader program:\n" + Text(diagnostics));
        }
        Slang::ComPtr<slang::IBlob> code;
        if (SLANG_FAILED(
                linked->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef()))) {
            throw std::invalid_argument("Cannot generate code for " + entryPoint + ":\n" +
                                        Text(diagnostics));
        }
        return code;
    }
};

ShaderCompiler::ShaderCompiler() : impl_(std::make_unique<Impl>()) {
    if (SLANG_FAILED(slang::createGlobalSession(impl_->global.writeRef()))) {
        throw std::runtime_error("Cannot initialise the Slang shader compiler");
    }
}

ShaderCompiler::~ShaderCompiler() = default;

std::vector<std::uint32_t> ShaderCompiler::CompileSpirv(std::span<const ShaderModuleSource> modules,
                                                        const std::string& entryPoint) const {
    const Slang::ComPtr<slang::IBlob> code =
        impl_->Compile(modules, entryPoint, SLANG_SPIRV, "spirv_1_6");
    std::vector<std::uint32_t> words(code->getBufferSize() / sizeof(std::uint32_t));
    std::memcpy(words.data(), code->getBufferPointer(), words.size() * sizeof(std::uint32_t));
    return words;
}

std::string ShaderCompiler::CompileWgsl(std::span<const ShaderModuleSource> modules,
                                        const std::string& entryPoint) const {
    return Text(impl_->Compile(modules, entryPoint, SLANG_WGSL, nullptr));
}

}  // namespace ernie
