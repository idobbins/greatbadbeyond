use std::env;
use std::fs;
use std::path::{Path, PathBuf};

fn main() {
    let shader_dir = Path::new("shaders");
    println!("cargo:rerun-if-changed={}", shader_dir.display());

    let out_dir = PathBuf::from(env::var("OUT_DIR").expect("OUT_DIR not set"));
    let compiler = shaderc::Compiler::new().expect("shaderc compiler");
    let mut options = shaderc::CompileOptions::new().expect("shaderc options");
    options.set_target_env(
        shaderc::TargetEnv::Vulkan,
        shaderc::EnvVersion::Vulkan1_2 as u32,
    );
    options.set_optimization_level(shaderc::OptimizationLevel::Performance);

    let shaders = [
        ("compute.comp.glsl", shaderc::ShaderKind::Compute),
        ("denoise.comp.glsl", shaderc::ShaderKind::Compute),
        ("blit.vert.glsl", shaderc::ShaderKind::Vertex),
        ("blit.frag.glsl", shaderc::ShaderKind::Fragment),
    ];

    for (file, kind) in shaders {
        let source_path = shader_dir.join(file);
        println!("cargo:rerun-if-changed={}", source_path.display());

        let source = fs::read_to_string(&source_path)
            .unwrap_or_else(|e| panic!("read {}: {e}", source_path.display()));
        let compiled_result = compiler
            .compile_into_spirv(&source, kind, file, "main", Some(&options))
            .unwrap_or_else(|e| panic!("compile {}: {e}", source_path.display()));

        let output_path = out_dir.join(format!("{file}.spv"));
        fs::write(&output_path, compiled_result.as_binary_u8())
            .unwrap_or_else(|e| panic!("write {}: {e}", output_path.display()));
    }
}
