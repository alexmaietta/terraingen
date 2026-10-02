struct Light {
    view_proj: mat4x4<f32>,
}

@group(1) @binding(0) var<uniform> light: Light;

@vertex
fn vs_shadow(@builtin(vertex_index) vertex_index: u32) -> @builtin(position) vec4<f32> {
    return light.view_proj * vec4<f32>(grid_vertex(vertex_index).world, 1.0);
}
