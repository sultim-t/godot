/*************************************************************************/
/*  rasterizer_scene_forward_rd.cpp                                      */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2019 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2019 Godot Engine contributors (cf. AUTHORS.md)    */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "rasterizer_scene_forward_rd.h"
#include "core/project_settings.h"
#include "servers/visual/rendering_device.h"
#include "servers/visual/visual_server_raster.h"

static _FORCE_INLINE_ void store_transform(const Transform &p_mtx, float *p_array) {
	p_array[0] = p_mtx.basis.elements[0][0];
	p_array[1] = p_mtx.basis.elements[1][0];
	p_array[2] = p_mtx.basis.elements[2][0];
	p_array[3] = 0;
	p_array[4] = p_mtx.basis.elements[0][1];
	p_array[5] = p_mtx.basis.elements[1][1];
	p_array[6] = p_mtx.basis.elements[2][1];
	p_array[7] = 0;
	p_array[8] = p_mtx.basis.elements[0][2];
	p_array[9] = p_mtx.basis.elements[1][2];
	p_array[10] = p_mtx.basis.elements[2][2];
	p_array[11] = 0;
	p_array[12] = p_mtx.origin.x;
	p_array[13] = p_mtx.origin.y;
	p_array[14] = p_mtx.origin.z;
	p_array[15] = 1;
}

static _FORCE_INLINE_ void store_transform_3x3(const Transform &p_mtx, float *p_array) {
	p_array[0] = p_mtx.basis.elements[0][0];
	p_array[1] = p_mtx.basis.elements[1][0];
	p_array[2] = p_mtx.basis.elements[2][0];
	p_array[3] = 0;
	p_array[4] = p_mtx.basis.elements[0][1];
	p_array[5] = p_mtx.basis.elements[1][1];
	p_array[6] = p_mtx.basis.elements[2][1];
	p_array[7] = 0;
	p_array[8] = p_mtx.basis.elements[0][2];
	p_array[9] = p_mtx.basis.elements[1][2];
	p_array[10] = p_mtx.basis.elements[2][2];
	p_array[11] = 0;
}

static _FORCE_INLINE_ void store_transform_3x3_430(const Transform &p_mtx, float *p_array) {
	p_array[0] = p_mtx.basis.elements[0][0];
	p_array[1] = p_mtx.basis.elements[1][0];
	p_array[2] = p_mtx.basis.elements[2][0];
	p_array[3] = p_mtx.basis.elements[0][1];
	p_array[4] = p_mtx.basis.elements[1][1];
	p_array[5] = p_mtx.basis.elements[2][1];
	p_array[6] = p_mtx.basis.elements[0][2];
	p_array[7] = p_mtx.basis.elements[1][2];
	p_array[8] = p_mtx.basis.elements[2][2];
}

static _FORCE_INLINE_ void store_camera(const CameraMatrix &p_mtx, float *p_array) {

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {

			p_array[i * 4 + j] = p_mtx.matrix[i][j];
		}
	}
}
void RasterizerSceneForwardRD::ShaderData::set_code(const String &p_code) {
	//compile

	code = p_code;
	valid = false;
	ubo_size = 0;
	uniforms.clear();
	uses_screen_texture = false;

	if (code == String()) {
		return; //just invalid, but no error
	}

	ShaderCompilerRD::GeneratedCode gen_code;

	int blend_mode = BLEND_MODE_MIX;
	int depth_testi = DEPTH_TEST_ENABLED;
	int cull = CULL_BACK;

	uses_point_size = false;
	uses_alpha = false;
	uses_blend_alpha = false;
	uses_depth_pre_pass = false;
	uses_discard = false;
	uses_roughness = false;
	uses_normal = false;
	bool wireframe = false;

	unshaded = false;
	uses_vertex = false;
	uses_sss = false;
	uses_screen_texture = false;
	uses_depth_texture = false;
	uses_normal_texture = false;
	uses_time = false;
	writes_modelview_or_projection = false;
	uses_world_coordinates = false;

	int depth_drawi = DEPTH_DRAW_OPAQUE;

	ShaderCompilerRD::IdentifierActions actions;

	actions.render_mode_values["blend_add"] = Pair<int *, int>(&blend_mode, BLEND_MODE_ADD);
	actions.render_mode_values["blend_mix"] = Pair<int *, int>(&blend_mode, BLEND_MODE_MIX);
	actions.render_mode_values["blend_sub"] = Pair<int *, int>(&blend_mode, BLEND_MODE_SUB);
	actions.render_mode_values["blend_mul"] = Pair<int *, int>(&blend_mode, BLEND_MODE_MUL);

	actions.render_mode_values["depth_draw_never"] = Pair<int *, int>(&depth_drawi, DEPTH_DRAW_DISABLED);
	actions.render_mode_values["depth_draw_opaque"] = Pair<int *, int>(&depth_drawi, DEPTH_DRAW_OPAQUE);
	actions.render_mode_values["depth_draw_always"] = Pair<int *, int>(&depth_drawi, DEPTH_DRAW_ALWAYS);

	actions.render_mode_values["depth_test_disabled"] = Pair<int *, int>(&depth_testi, DEPTH_TEST_DISABLED);

	actions.render_mode_values["cull_disabled"] = Pair<int *, int>(&cull, CULL_DISABLED);
	actions.render_mode_values["cull_front"] = Pair<int *, int>(&cull, CULL_FRONT);
	actions.render_mode_values["cull_back"] = Pair<int *, int>(&cull, CULL_BACK);

	actions.render_mode_flags["unshaded"] = &unshaded;
	actions.render_mode_flags["wireframe"] = &wireframe;

	actions.usage_flag_pointers["ALPHA"] = &uses_alpha;
	actions.render_mode_flags["depth_prepass_alpha"] = &uses_depth_pre_pass;

	actions.usage_flag_pointers["SSS_STRENGTH"] = &uses_sss;

	actions.usage_flag_pointers["SCREEN_TEXTURE"] = &uses_screen_texture;
	actions.usage_flag_pointers["DEPTH_TEXTURE"] = &uses_depth_texture;
	actions.usage_flag_pointers["NORMAL_TEXTURE"] = &uses_normal_texture;
	actions.usage_flag_pointers["DISCARD"] = &uses_discard;
	actions.usage_flag_pointers["TIME"] = &uses_time;
	actions.usage_flag_pointers["ROUGHNESS"] = &uses_roughness;
	actions.usage_flag_pointers["NORMAL"] = &uses_normal;
	actions.usage_flag_pointers["NORMALMAP"] = &uses_normal;

	actions.usage_flag_pointers["POINT_SIZE"] = &uses_point_size;
	actions.usage_flag_pointers["POINT_COORD"] = &uses_point_size;

	actions.write_flag_pointers["MODELVIEW_MATRIX"] = &writes_modelview_or_projection;
	actions.write_flag_pointers["PROJECTION_MATRIX"] = &writes_modelview_or_projection;
	actions.write_flag_pointers["VERTEX"] = &uses_vertex;

	actions.uniforms = &uniforms;

	RasterizerSceneForwardRD *scene_singleton = (RasterizerSceneForwardRD *)RasterizerSceneForwardRD::singleton;

	Error err = scene_singleton->shader.compiler.compile(VS::SHADER_SPATIAL, code, &actions, path, gen_code);

	ERR_FAIL_COND(err != OK);

	if (version.is_null()) {
		version = scene_singleton->shader.scene_shader.version_create();
	}

	depth_draw = DepthDraw(depth_drawi);
	depth_test = DepthTest(depth_testi);

#if 0
	print_line("**compiling shader:");
	print_line("**defines:\n");
	for (int i = 0; i < gen_code.defines.size(); i++) {
		print_line(gen_code.defines[i]);
	}
	print_line("\n**uniforms:\n" + gen_code.uniforms);
	print_line("\n**vertex_globals:\n" + gen_code.vertex_global);
	print_line("\n**vertex_code:\n" + gen_code.vertex);
	print_line("\n**fragment_globals:\n" + gen_code.fragment_global);
	print_line("\n**fragment_code:\n" + gen_code.fragment);
	print_line("\n**light_code:\n" + gen_code.light);
#endif
	scene_singleton->shader.scene_shader.version_set_code(version, gen_code.uniforms, gen_code.vertex_global, gen_code.vertex, gen_code.fragment_global, gen_code.light, gen_code.fragment, gen_code.defines);
	ERR_FAIL_COND(!scene_singleton->shader.scene_shader.version_is_valid(version));

	ubo_size = gen_code.uniform_total_size;
	ubo_offsets = gen_code.uniform_offsets;
	texture_uniforms = gen_code.texture_uniforms;

	//blend modes

	RD::PipelineColorBlendState::Attachment blend_attachment;

	switch (blend_mode) {
		case BLEND_MODE_MIX: {

			blend_attachment.enable_blend = true;
			blend_attachment.alpha_blend_op = RD::BLEND_OP_ADD;
			blend_attachment.color_blend_op = RD::BLEND_OP_ADD;
			blend_attachment.src_color_blend_factor = RD::BLEND_FACTOR_SRC_ALPHA;
			blend_attachment.dst_color_blend_factor = RD::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blend_attachment.src_alpha_blend_factor = RD::BLEND_FACTOR_ONE;
			blend_attachment.dst_alpha_blend_factor = RD::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

		} break;
		case BLEND_MODE_ADD: {

			blend_attachment.enable_blend = true;
			blend_attachment.alpha_blend_op = RD::BLEND_OP_ADD;
			blend_attachment.color_blend_op = RD::BLEND_OP_ADD;
			blend_attachment.src_color_blend_factor = RD::BLEND_FACTOR_SRC_ALPHA;
			blend_attachment.dst_color_blend_factor = RD::BLEND_FACTOR_ONE;
			blend_attachment.src_alpha_blend_factor = RD::BLEND_FACTOR_SRC_ALPHA;
			blend_attachment.dst_alpha_blend_factor = RD::BLEND_FACTOR_ONE;
			uses_blend_alpha = true; //force alpha used because of blend

		} break;
		case BLEND_MODE_SUB: {

			blend_attachment.enable_blend = true;
			blend_attachment.alpha_blend_op = RD::BLEND_OP_SUBTRACT;
			blend_attachment.color_blend_op = RD::BLEND_OP_SUBTRACT;
			blend_attachment.src_color_blend_factor = RD::BLEND_FACTOR_SRC_ALPHA;
			blend_attachment.dst_color_blend_factor = RD::BLEND_FACTOR_ONE;
			blend_attachment.src_alpha_blend_factor = RD::BLEND_FACTOR_SRC_ALPHA;
			blend_attachment.dst_alpha_blend_factor = RD::BLEND_FACTOR_ONE;
			uses_blend_alpha = true; //force alpha used because of blend

		} break;
		case BLEND_MODE_MUL: {
			blend_attachment.enable_blend = true;
			blend_attachment.alpha_blend_op = RD::BLEND_OP_ADD;
			blend_attachment.color_blend_op = RD::BLEND_OP_ADD;
			blend_attachment.src_color_blend_factor = RD::BLEND_FACTOR_DST_COLOR;
			blend_attachment.dst_color_blend_factor = RD::BLEND_FACTOR_ZERO;
			blend_attachment.src_alpha_blend_factor = RD::BLEND_FACTOR_DST_ALPHA;
			blend_attachment.dst_alpha_blend_factor = RD::BLEND_FACTOR_ZERO;
			uses_blend_alpha = true; //force alpha used because of blend
		} break;
	}

	RD::PipelineColorBlendState blend_state_blend;
	blend_state_blend.attachments.push_back(blend_attachment);
	RD::PipelineColorBlendState blend_state_opaque = RD::PipelineColorBlendState::create_disabled(1);
	RD::PipelineColorBlendState blend_state_opaque_specular = RD::PipelineColorBlendState::create_disabled(2);

	//update pipelines

	RD::PipelineDepthStencilState depth_stencil_state;

	if (depth_test != DEPTH_TEST_DISABLED) {
		depth_stencil_state.enable_depth_test = true;
		depth_stencil_state.depth_compare_operator = RD::COMPARE_OP_LESS_OR_EQUAL;
		depth_stencil_state.enable_depth_write = depth_draw != DEPTH_DRAW_DISABLED ? true : false;
	}

	for (int i = 0; i < CULL_VARIANT_MAX; i++) {

		RD::PolygonCullMode cull_mode_rd_table[3][CULL_VARIANT_MAX] = {
			{ RD::POLYGON_CULL_DISABLED, RD::POLYGON_CULL_FRONT, RD::POLYGON_CULL_BACK },
			{ RD::POLYGON_CULL_DISABLED, RD::POLYGON_CULL_BACK, RD::POLYGON_CULL_FRONT },
			{ RD::POLYGON_CULL_DISABLED, RD::POLYGON_CULL_DISABLED, RD::POLYGON_CULL_DISABLED }
		};

		RD::PolygonCullMode cull_mode_rd = cull_mode_rd_table[cull][i];

		for (int j = 0; j < VS::PRIMITIVE_MAX; j++) {

			RD::RenderPrimitive primitive_rd_table[VS::PRIMITIVE_MAX] = {
				RD::RENDER_PRIMITIVE_POINTS,
				RD::RENDER_PRIMITIVE_LINES,
				RD::RENDER_PRIMITIVE_LINESTRIPS,
				RD::RENDER_PRIMITIVE_TRIANGLES,
				RD::RENDER_PRIMITIVE_TRIANGLE_STRIPS,
			};

			RD::RenderPrimitive primitive_rd = uses_point_size ? RD::RENDER_PRIMITIVE_POINTS : primitive_rd_table[j];

			for (int k = 0; k < SHADER_VERSION_MAX; k++) {

				RD::PipelineRasterizationState raster_state;
				raster_state.cull_mode = cull_mode_rd;
				raster_state.wireframe = wireframe;

				RD::PipelineColorBlendState blend_state;
				RD::PipelineDepthStencilState depth_stencil = depth_stencil_state;

				if (uses_alpha || uses_blend_alpha) {
					if (k == SHADER_VERSION_COLOR_PASS || k == SHADER_VERSION_VCT_COLOR_PASS || k == SHADER_VERSION_LIGHTMAP_COLOR_PASS) {
						blend_state = blend_state_blend;
						if (depth_draw == DEPTH_DRAW_OPAQUE) {
							depth_stencil.enable_depth_write = false; //alpha does not draw depth
						}
					} else if (uses_depth_pre_pass && (k == SHADER_VERSION_DEPTH_PASS || k == SHADER_VERSION_DEPTH_PASS_DP || k == SHADER_VERSION_DEPTH_PASS_WITH_NORMAL || k == SHADER_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS)) {
						if (k == SHADER_VERSION_DEPTH_PASS || k == k == SHADER_VERSION_DEPTH_PASS_DP) {
							//none, blend state contains nothing
						} else {
							blend_state = blend_state_opaque; //writes to normal and roughness in opaque way
						}
					} else {
						pipelines[i][j][k].clear();
						continue; // do not use this version (will error if using it is attempted)
					}
				} else {

					if (k == SHADER_VERSION_COLOR_PASS || k == SHADER_VERSION_VCT_COLOR_PASS || k == SHADER_VERSION_LIGHTMAP_COLOR_PASS) {
						blend_state = blend_state_opaque;
					} else if (k == SHADER_VERSION_DEPTH_PASS || k == SHADER_VERSION_DEPTH_PASS_DP) {
						//none, leave empty
					} else if (k == SHADER_VERSION_DEPTH_PASS_WITH_NORMAL || k == SHADER_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS) {
						blend_state = blend_state_opaque; //writes to normal and roughness in opaque way
					} else {
						//specular write
						blend_state = blend_state_opaque_specular;
					}
				}

				RID shader_variant = scene_singleton->shader.scene_shader.version_get_shader(version, k);
				pipelines[i][j][k].setup(shader_variant, primitive_rd, raster_state, RD::PipelineMultisampleState(), depth_stencil, blend_state, 0);
			}
		}
	}

	valid = true;
}

void RasterizerSceneForwardRD::ShaderData::set_default_texture_param(const StringName &p_name, RID p_texture) {
	if (!p_texture.is_valid()) {
		default_texture_params.erase(p_name);
	} else {
		default_texture_params[p_name] = p_texture;
	}
}
void RasterizerSceneForwardRD::ShaderData::get_param_list(List<PropertyInfo> *p_param_list) const {

	Map<int, StringName> order;

	for (Map<StringName, ShaderLanguage::ShaderNode::Uniform>::Element *E = uniforms.front(); E; E = E->next()) {

		if (E->get().texture_order >= 0) {
			order[E->get().texture_order + 100000] = E->key();
		} else {
			order[E->get().order] = E->key();
		}
	}

	for (Map<int, StringName>::Element *E = order.front(); E; E = E->next()) {

		PropertyInfo pi = ShaderLanguage::uniform_to_property_info(uniforms[E->get()]);
		pi.name = E->get();
		p_param_list->push_back(pi);
	}
}

bool RasterizerSceneForwardRD::ShaderData::is_param_texture(const StringName &p_param) const {
	if (!uniforms.has(p_param)) {
		return false;
	}

	return uniforms[p_param].texture_order >= 0;
}

bool RasterizerSceneForwardRD::ShaderData::is_animated() const {
	return false;
}
bool RasterizerSceneForwardRD::ShaderData::casts_shadows() const {
	return false;
}
Variant RasterizerSceneForwardRD::ShaderData::get_default_parameter(const StringName &p_parameter) const {
	if (uniforms.has(p_parameter)) {
		ShaderLanguage::ShaderNode::Uniform uniform = uniforms[p_parameter];
		Vector<ShaderLanguage::ConstantNode::Value> default_value = uniform.default_value;
		return ShaderLanguage::constant_value_to_variant(default_value, uniform.type, uniform.hint);
	}
	return Variant();
}

RasterizerSceneForwardRD::ShaderData::ShaderData() {
	valid = false;
	uses_screen_texture = false;
}

RasterizerSceneForwardRD::ShaderData::~ShaderData() {
	RasterizerSceneForwardRD *scene_singleton = (RasterizerSceneForwardRD *)RasterizerSceneForwardRD::singleton;
	ERR_FAIL_COND(!scene_singleton);
	//pipeline variants will clear themselves if shader is gone
	if (version.is_valid()) {
		scene_singleton->shader.scene_shader.version_free(version);
	}
}

RasterizerStorageRD::ShaderData *RasterizerSceneForwardRD::_create_shader_func() {
	ShaderData *shader_data = memnew(ShaderData);
	return shader_data;
}

void RasterizerSceneForwardRD::MaterialData::set_render_priority(int p_priority) {
	priority = p_priority - VS::MATERIAL_RENDER_PRIORITY_MIN; //8 bits
}

void RasterizerSceneForwardRD::MaterialData::set_next_pass(RID p_pass) {
	next_pass = p_pass;
}

void RasterizerSceneForwardRD::MaterialData::update_parameters(const Map<StringName, Variant> &p_parameters, bool p_uniform_dirty, bool p_textures_dirty) {

	RasterizerSceneForwardRD *scene_singleton = (RasterizerSceneForwardRD *)RasterizerSceneForwardRD::singleton;

	if ((uint32_t)ubo_data.size() != shader_data->ubo_size) {
		p_uniform_dirty = true;
		if (uniform_buffer.is_valid()) {
			RD::get_singleton()->free(uniform_buffer);
			uniform_buffer = RID();
		}

		ubo_data.resize(shader_data->ubo_size);
		if (ubo_data.size()) {
			uniform_buffer = RD::get_singleton()->uniform_buffer_create(ubo_data.size());
			memset(ubo_data.ptrw(), 0, ubo_data.size()); //clear
		}

		//clear previous uniform set
		if (uniform_set.is_valid() && RD::get_singleton()->uniform_set_is_valid(uniform_set)) {
			RD::get_singleton()->free(uniform_set);
			uniform_set = RID();
		}
	}

	//check whether buffer changed
	if (p_uniform_dirty && ubo_data.size()) {

		update_uniform_buffer(shader_data->uniforms, shader_data->ubo_offsets.ptr(), p_parameters, ubo_data.ptrw(), ubo_data.size(), false);
		RD::get_singleton()->buffer_update(uniform_buffer, 0, ubo_data.size(), ubo_data.ptrw());
	}

	uint32_t tex_uniform_count = shader_data->texture_uniforms.size();

	if ((uint32_t)texture_cache.size() != tex_uniform_count) {
		texture_cache.resize(tex_uniform_count);
		p_textures_dirty = true;

		//clear previous uniform set
		if (uniform_set.is_valid() && RD::get_singleton()->uniform_set_is_valid(uniform_set)) {
			RD::get_singleton()->free(uniform_set);
			uniform_set = RID();
		}
	}

	if (p_textures_dirty && tex_uniform_count) {

		update_textures(p_parameters, shader_data->default_texture_params, shader_data->texture_uniforms, texture_cache.ptrw());
	}

	if (shader_data->ubo_size == 0 && shader_data->texture_uniforms.size() == 0) {
		// This material does not require an uniform set, so don't create it.
		return;
	}

	if (!p_textures_dirty && uniform_set.is_valid() && RD::get_singleton()->uniform_set_is_valid(uniform_set)) {
		//no reason to update uniform set, only UBO (or nothing) was needed to update
		return;
	}

	Vector<RD::Uniform> uniforms;

	{

		if (shader_data->ubo_size) {
			RD::Uniform u;
			u.type = RD::UNIFORM_TYPE_UNIFORM_BUFFER;
			u.binding = 0;
			u.ids.push_back(uniform_buffer);
			uniforms.push_back(u);
		}

		const RID *textures = texture_cache.ptrw();
		for (uint32_t i = 0; i < tex_uniform_count; i++) {
			RD::Uniform u;
			u.type = RD::UNIFORM_TYPE_TEXTURE;
			u.binding = 1 + i;
			u.ids.push_back(textures[i]);
			uniforms.push_back(u);
		}
	}

	uniform_set = RD::get_singleton()->uniform_set_create(uniforms, scene_singleton->shader.scene_shader.version_get_shader(shader_data->version, 0), 2);
}
RasterizerSceneForwardRD::MaterialData::~MaterialData() {
	if (uniform_set.is_valid() && RD::get_singleton()->uniform_set_is_valid(uniform_set)) {
		RD::get_singleton()->free(uniform_set);
	}

	if (uniform_buffer.is_valid()) {
		RD::get_singleton()->free(uniform_buffer);
	}
}

RasterizerStorageRD::MaterialData *RasterizerSceneForwardRD::_create_material_func(ShaderData *p_shader) {
	MaterialData *material_data = memnew(MaterialData);
	material_data->shader_data = p_shader;
	material_data->last_frame = false;
	//update will happen later anyway so do nothing.
	return material_data;
}

RasterizerSceneForwardRD::RenderBufferDataForward::~RenderBufferDataForward() {
	clear();
}

void RasterizerSceneForwardRD::RenderBufferDataForward::clear() {

	if (color_fb.is_valid()) {
		RD::get_singleton()->free(color_fb);
		color_fb = RID();
	}

	if (color.is_valid()) {
		RD::get_singleton()->free(color);
		color = RID();
	}

	if (depth.is_valid()) {
		RD::get_singleton()->free(depth);
		depth = RID();
	}
}

void RasterizerSceneForwardRD::RenderBufferDataForward::configure(RID p_render_target, int p_width, int p_height, VS::ViewportMSAA p_msaa) {
	clear();

	width = p_width;
	height = p_height;

	render_target = p_render_target;

	{
		RD::TextureFormat tf;
		tf.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
		tf.width = p_width;
		tf.height = p_height;
		tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT;

		color = RD::get_singleton()->texture_create(tf, RD::TextureView());
	}
	{
		RD::TextureFormat tf;
		tf.format = RD::get_singleton()->texture_is_format_supported_for_usage(RD::DATA_FORMAT_D24_UNORM_S8_UINT, RD::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ? RD::DATA_FORMAT_D24_UNORM_S8_UINT : RD::DATA_FORMAT_D32_SFLOAT_S8_UINT;
		tf.width = p_width;
		tf.height = p_height;
		tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

		depth = RD::get_singleton()->texture_create(tf, RD::TextureView());
	}

	{
		Vector<RID> fb;
		fb.push_back(color);
		fb.push_back(depth);

		color_fb = RD::get_singleton()->framebuffer_create(fb);
	}

	{
		Vector<RID> fb;
		fb.push_back(color);

		color_only_fb = RD::get_singleton()->framebuffer_create(fb);
	}
}

RasterizerSceneRD::RenderBufferData *RasterizerSceneForwardRD::_create_render_buffer_data() {
	return memnew(RenderBufferDataForward);
}

bool RasterizerSceneForwardRD::free(RID p_rid) {
	if (RasterizerSceneRD::free(p_rid)) {
		return true;
	}
	return false;
}

void RasterizerSceneForwardRD::_fill_instances(RenderList::Element **p_elements, int p_element_count) {

	for (int i = 0; i < p_element_count; i++) {

		const RenderList::Element *e = p_elements[i];
		InstanceData &id = scene_state.instances[i];
		store_transform(e->instance->transform, id.transform);
		store_transform(Transform(e->instance->transform.basis.inverse().transposed()), id.normal_transform);
		id.flags = 0;
		id.mask = e->instance->layer_mask;

		//forward

		uint32_t reflection_count = 0;
		uint32_t omni_count = 0;
		uint32_t spot_count = 0;
		uint32_t decal_count = 0;

		if (!e->instance->reflection_probe_instances.empty()) {

			uint32_t rpi_count = e->instance->reflection_probe_instances.size();
			const RID *rpi_ptrs = e->instance->reflection_probe_instances.ptr();

			for (uint32_t j = 0; j < rpi_count; j++) {
				if (render_pass != reflection_probe_instance_get_render_pass(rpi_ptrs[j])) {
					continue; //not rendered this frame
				}

				RID base = reflection_probe_instance_get_probe(rpi_ptrs[j]);

				uint32_t mask = storage->reflection_probe_get_cull_mask(base);
				if (!(mask & id.mask)) {
					continue; //masked
				}

				if (reflection_count < 8) {
					id.omni_light_indices[omni_count] = reflection_probe_instance_get_render_index(rpi_ptrs[j]);
					reflection_count++;
				}
			}
		}

		if (!e->instance->light_instances.empty()) {
			uint32_t light_count = e->instance->light_instances.size();
			const RID *light_ptrs = e->instance->light_instances.ptr();

			for (uint32_t j = 0; j < light_count; j++) {
				if (render_pass != light_instance_get_render_pass(light_ptrs[j])) {
					continue; //not rendered this frame
				}

				RID base = light_instance_get_base_light(light_ptrs[j]);

				uint32_t mask = storage->light_get_cull_mask(base);
				if (!(mask & id.mask)) {
					continue; //masked
				}

				if (storage->light_get_type(base) == VS::LIGHT_OMNI) {
					if (omni_count < 8) {
						id.omni_light_indices[omni_count] = light_instance_get_index(light_ptrs[j]);
						omni_count++;
					}
				} else {
					if (spot_count < 8) {
						id.omni_light_indices[spot_count] = light_instance_get_index(light_ptrs[j]);
						spot_count++;
					}
				}
			}
		}

		id.flags |= reflection_count;
		id.flags |= omni_count << 3;
		id.flags |= spot_count << 6;
		id.flags |= decal_count << 9;
	}

	RD::get_singleton()->buffer_update(scene_state.instance_buffer, 0, sizeof(InstanceData) * p_element_count, scene_state.instances, true);
}

/// RENDERING ///

void RasterizerSceneForwardRD::_render_list(RenderingDevice::DrawListID p_draw_list, RenderingDevice::FramebufferFormatID p_framebuffer_Format, RenderList::Element **p_elements, int p_element_count, bool p_reverse_cull, PassMode p_pass_mode, bool p_no_gi) {

	RD::DrawListID draw_list = p_draw_list;
	RD::FramebufferFormatID framebuffer_format = p_framebuffer_Format;

	//global scope bindings
	RD::get_singleton()->draw_list_bind_uniform_set(draw_list, render_base_uniform_set, 0);

	MaterialData *prev_material = nullptr;
	//	ShaderData *prev_shader = nullptr;

	RID prev_vertex_array_rd;
	RID prev_index_array_rd;
	RID prev_pipeline_rd;

	PushConstant push_constant;
	zeromem(&push_constant, sizeof(PushConstant));

	for (int i = 0; i < p_element_count; i++) {

		const RenderList::Element *e = p_elements[i];

		MaterialData *material = e->material;
		ShaderData *shader = material->shader_data;

		//find cull variant
		ShaderData::CullVariant cull_variant;

		if ((p_pass_mode == PASS_MODE_SHADOW || p_pass_mode == PASS_MODE_SHADOW_DP) && e->instance->cast_shadows == VS::SHADOW_CASTING_SETTING_DOUBLE_SIDED) {
			cull_variant = ShaderData::CULL_VARIANT_DOUBLE_SIDED;
		} else {
			bool mirror = e->instance->mirror;
			if (p_reverse_cull) {
				mirror = !mirror;
			}
			cull_variant = mirror ? ShaderData::CULL_VARIANT_REVERSED : ShaderData::CULL_VARIANT_NORMAL;
		}

		//find primitive and vertex format
		VS::PrimitiveType primitive;

		switch (e->instance->base_type) {
			case VS::INSTANCE_MESH: {
				primitive = storage->mesh_surface_get_primitive(e->instance->base, e->surface_index);
			} break;
			case VS::INSTANCE_MULTIMESH: {
				ERR_CONTINUE(true); //should be a bug
			} break;
			case VS::INSTANCE_IMMEDIATE: {
				ERR_CONTINUE(true); //should be a bug
			} break;
			case VS::INSTANCE_PARTICLES: {
				ERR_CONTINUE(true); //should be a bug
			} break;
			default: {
				ERR_CONTINUE(true); //should be a bug
			}
		}

		ShaderVersion shader_version;

		switch (p_pass_mode) {
			case PASS_MODE_COLOR:
			case PASS_MODE_COLOR_TRANSPARENT: {

				if (e->uses_lightmap) {
					shader_version = SHADER_VERSION_LIGHTMAP_COLOR_PASS;
				} else if (e->uses_vct) {
					shader_version = SHADER_VERSION_VCT_COLOR_PASS;
				} else {
					shader_version = SHADER_VERSION_COLOR_PASS;
				}

			} break;
			case PASS_MODE_COLOR_SPECULAR: {
				if (e->uses_lightmap) {
					shader_version = SHADER_VERSION_LIGHTMAP_COLOR_PASS_WITH_SEPARATE_SPECULAR;
				} else if (e->uses_vct) {
					shader_version = SHADER_VERSION_VCT_COLOR_PASS_WITH_SEPARATE_SPECULAR;
				} else {
					shader_version = SHADER_VERSION_COLOR_PASS_WITH_SEPARATE_SPECULAR;
				}
			} break;
			case PASS_MODE_SHADOW:
			case PASS_MODE_DEPTH: {
				shader_version = SHADER_VERSION_DEPTH_PASS;
			} break;
			case PASS_MODE_SHADOW_DP: {
				shader_version = SHADER_VERSION_DEPTH_PASS_DP;
			} break;
			case PASS_MODE_DEPTH_NORMAL: {
				shader_version = SHADER_VERSION_DEPTH_PASS_WITH_NORMAL;
			} break;
			case PASS_MODE_DEPTH_NORMAL_ROUGHNESS: {
				shader_version = SHADER_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS;
			} break;
		}

		RenderPipelineVertexFormatCacheRD *pipeline = nullptr;

		pipeline = &shader->pipelines[cull_variant][primitive][shader_version];

		RD::VertexFormatID vertex_format;
		RID vertex_array_rd;
		RID index_array_rd;

		switch (e->instance->base_type) {
			case VS::INSTANCE_MESH: {
				storage->mesh_surface_get_arrays_and_format(e->instance->base, e->surface_index, pipeline->get_vertex_input_mask(), vertex_array_rd, index_array_rd, vertex_format);
			} break;
			case VS::INSTANCE_MULTIMESH: {
				ERR_CONTINUE(true); //should be a bug
			} break;
			case VS::INSTANCE_IMMEDIATE: {
				ERR_CONTINUE(true); //should be a bug
			} break;
			case VS::INSTANCE_PARTICLES: {
				ERR_CONTINUE(true); //should be a bug
			} break;
			default: {
				ERR_CONTINUE(true); //should be a bug
			}
		}

		if (prev_vertex_array_rd != vertex_array_rd) {
			RD::get_singleton()->draw_list_bind_vertex_array(draw_list, vertex_array_rd);
			prev_vertex_array_rd = vertex_array_rd;
		}

		if (prev_index_array_rd != index_array_rd) {
			if (index_array_rd.is_valid()) {
				RD::get_singleton()->draw_list_bind_index_array(draw_list, index_array_rd);
			}
			prev_index_array_rd = index_array_rd;
		}

		RID pipeline_rd = pipeline->get_render_pipeline(vertex_format, framebuffer_format);

		if (pipeline_rd != prev_pipeline_rd) {
			// checking with prev shader does not make so much sense, as
			// the pipeline may still be different.
			RD::get_singleton()->draw_list_bind_render_pipeline(draw_list, pipeline_rd);
			prev_pipeline_rd = pipeline_rd;
		}

		if (material != prev_material) {
			//update uniform set
			if (material->uniform_set.is_valid()) {
				RD::get_singleton()->draw_list_bind_uniform_set(draw_list, material->uniform_set, 2);
			}

			prev_material = material;
		}

		push_constant.index = i;
		RD::get_singleton()->draw_list_set_push_constant(draw_list, &push_constant, sizeof(PushConstant));

		switch (e->instance->base_type) {
			case VS::INSTANCE_MESH: {
				RD::get_singleton()->draw_list_draw(draw_list, index_array_rd.is_valid());
			} break;
			case VS::INSTANCE_MULTIMESH: {

			} break;
			case VS::INSTANCE_IMMEDIATE: {

			} break;
			case VS::INSTANCE_PARTICLES: {

			} break;
			default: {
				ERR_CONTINUE(true); //should be a bug
			}
		}
	}
}

void RasterizerSceneForwardRD::_setup_environment(RID p_render_target, RID p_environment, const CameraMatrix &p_cam_projection, const Transform &p_cam_transform, RID p_reflection_probe, bool p_no_fog, const Size2 &p_screen_pixel_size, RID p_shadow_atlas) {

	//CameraMatrix projection = p_cam_projection;
	//projection.flip_y(); // Vulkan and modern APIs use Y-Down
	CameraMatrix correction;
	correction.set_depth_correction(!p_reflection_probe.is_valid());
	CameraMatrix projection = correction * p_cam_projection;

	//store camera into ubo
	store_camera(projection, scene_state.ubo.projection_matrix);
	store_camera(projection.inverse(), scene_state.ubo.inv_projection_matrix);
	store_transform(p_cam_transform, scene_state.ubo.camera_matrix);
	store_transform(p_cam_transform.affine_inverse(), scene_state.ubo.inv_camera_matrix);

	scene_state.ubo.screen_pixel_size[0] = p_screen_pixel_size.x;
	scene_state.ubo.screen_pixel_size[1] = p_screen_pixel_size.y;

	if (p_shadow_atlas.is_valid()) {
		Vector2 sas = shadow_atlas_get_size(p_shadow_atlas);
		scene_state.ubo.shadow_atlas_pixel_size[0] = 1.0 / sas.x;
		scene_state.ubo.shadow_atlas_pixel_size[1] = 1.0 / sas.y;
	}
	{
		Vector2 dss = directional_shadow_get_size();
		scene_state.ubo.directional_shadow_pixel_size[0] = 1.0 / dss.x;
		scene_state.ubo.directional_shadow_pixel_size[1] = 1.0 / dss.y;
	}
	//time global variables
	scene_state.ubo.time = time;

	if (is_environment(p_environment)) {

		VS::EnvironmentBG env_bg = environment_get_background(p_environment);
		VS::EnvironmentAmbientSource ambient_src = environment_get_ambient_light_ambient_source(p_environment);

		float bg_energy = environment_get_bg_energy(p_environment);
		scene_state.ubo.ambient_light_color_energy[3] = bg_energy;

		scene_state.ubo.ambient_color_sky_mix = environment_get_ambient_sky_contribution(p_environment);

		//ambient
		if (ambient_src == VS::ENV_AMBIENT_SOURCE_BG && (env_bg == VS::ENV_BG_CLEAR_COLOR || env_bg == VS::ENV_BG_COLOR)) {

			Color color = (p_render_target.is_valid() && env_bg == VS::ENV_BG_CLEAR_COLOR) ? (p_render_target.is_valid() ? storage->render_target_get_clear_request_color(p_render_target) : Color(0, 0, 0)) : environment_get_bg_color(p_environment);
			color = color.to_linear();

			scene_state.ubo.ambient_light_color_energy[0] = color.r * bg_energy;
			scene_state.ubo.ambient_light_color_energy[1] = color.g * bg_energy;
			scene_state.ubo.ambient_light_color_energy[2] = color.b * bg_energy;
			scene_state.ubo.use_ambient_light = true;
			scene_state.ubo.use_ambient_cubemap = false;
		} else {

			float energy = environment_get_ambient_light_ambient_energy(p_environment);
			Color color = environment_get_ambient_light_color(p_environment);
			color = color.to_linear();
			scene_state.ubo.ambient_light_color_energy[0] = color.r * energy;
			scene_state.ubo.ambient_light_color_energy[1] = color.g * energy;
			scene_state.ubo.ambient_light_color_energy[2] = color.b * energy;

			Basis sky_transform = environment_get_sky_orientation(p_environment);
			sky_transform = sky_transform.inverse() * p_cam_transform.basis;
			store_transform_3x3(sky_transform, scene_state.ubo.radiance_inverse_xform);

			scene_state.ubo.use_ambient_cubemap = (ambient_src == VS::ENV_AMBIENT_SOURCE_BG && env_bg == VS::ENV_BG_SKY) || ambient_src == VS::ENV_AMBIENT_SOURCE_SKY;
			scene_state.ubo.use_ambient_light = scene_state.ubo.use_ambient_cubemap || ambient_src == VS::ENV_AMBIENT_SOURCE_COLOR;
		}

		//specular
		VS::EnvironmentReflectionSource ref_src = environment_get_reflection_source(p_environment);
		if ((ref_src == VS::ENV_REFLECTION_SOURCE_BG && env_bg == VS::ENV_BG_SKY) || ref_src == VS::ENV_REFLECTION_SOURCE_SKY) {
			scene_state.ubo.use_reflection_cubemap = true;
		} else {
			scene_state.ubo.use_reflection_cubemap = false;
		}

	} else {

		if (p_reflection_probe.is_valid() && !storage->reflection_probe_is_interior(reflection_probe_instance_get_probe(p_reflection_probe))) {
			scene_state.ubo.use_ambient_light = true;
			Color clear_color = storage->get_default_clear_color();
			clear_color = clear_color.to_linear();
			scene_state.ubo.ambient_light_color_energy[0] = clear_color.r;
			scene_state.ubo.ambient_light_color_energy[1] = clear_color.g;
			scene_state.ubo.ambient_light_color_energy[2] = clear_color.b;
			scene_state.ubo.ambient_light_color_energy[3] = 1.0;

		} else if (p_render_target.is_valid()) {
			scene_state.ubo.use_ambient_light = true;
			Color clear_color = storage->render_target_get_clear_request_color(p_render_target);
			clear_color = clear_color.to_linear();
			scene_state.ubo.ambient_light_color_energy[0] = clear_color.r;
			scene_state.ubo.ambient_light_color_energy[1] = clear_color.g;
			scene_state.ubo.ambient_light_color_energy[2] = clear_color.b;
			scene_state.ubo.ambient_light_color_energy[3] = 1.0;
		} else {
			scene_state.ubo.use_ambient_light = false;
		}

		scene_state.ubo.use_ambient_cubemap = false;
		scene_state.ubo.use_reflection_cubemap = false;
	}
#if 0
	//bg and ambient
	if (p_environment.is_valid()) {

		state.ubo_data.bg_energy = env->bg_energy;
		state.ubo_data.ambient_energy = env->ambient_energy;
		Color linear_ambient_color = env->ambient_color.to_linear();
		state.ubo_data.ambient_light_color[0] = linear_ambient_color.r;
		state.ubo_data.ambient_light_color[1] = linear_ambient_color.g;
		state.ubo_data.ambient_light_color[2] = linear_ambient_color.b;
		state.ubo_data.ambient_light_color[3] = linear_ambient_color.a;

		Color bg_color;

		switch (env->bg_mode) {
			case VS::ENV_BG_CLEAR_COLOR: {
				bg_color = storage->frame.clear_request_color.to_linear();
			} break;
			case VS::ENV_BG_COLOR: {
				bg_color = env->bg_color.to_linear();
			} break;
			default: {
				bg_color = Color(0, 0, 0, 1);
			} break;
		}

		state.ubo_data.bg_color[0] = bg_color.r;
		state.ubo_data.bg_color[1] = bg_color.g;
		state.ubo_data.bg_color[2] = bg_color.b;
		state.ubo_data.bg_color[3] = bg_color.a;

		//use the inverse of our sky_orientation, we may need to skip this if we're using a reflection probe?
		sky_orientation = Transform(env->sky_orientation, Vector3(0.0, 0.0, 0.0)).affine_inverse();

		state.env_radiance_data.ambient_contribution = env->ambient_sky_contribution;
		state.ubo_data.ambient_occlusion_affect_light = env->ssao_light_affect;
		state.ubo_data.ambient_occlusion_affect_ssao = env->ssao_ao_channel_affect;

		//fog

		Color linear_fog = env->fog_color.to_linear();
		state.ubo_data.fog_color_enabled[0] = linear_fog.r;
		state.ubo_data.fog_color_enabled[1] = linear_fog.g;
		state.ubo_data.fog_color_enabled[2] = linear_fog.b;
		state.ubo_data.fog_color_enabled[3] = (!p_no_fog && env->fog_enabled) ? 1.0 : 0.0;
		state.ubo_data.fog_density = linear_fog.a;

		Color linear_sun = env->fog_sun_color.to_linear();
		state.ubo_data.fog_sun_color_amount[0] = linear_sun.r;
		state.ubo_data.fog_sun_color_amount[1] = linear_sun.g;
		state.ubo_data.fog_sun_color_amount[2] = linear_sun.b;
		state.ubo_data.fog_sun_color_amount[3] = env->fog_sun_amount;
		state.ubo_data.fog_depth_enabled = env->fog_depth_enabled;
		state.ubo_data.fog_depth_begin = env->fog_depth_begin;
		state.ubo_data.fog_depth_end = env->fog_depth_end;
		state.ubo_data.fog_depth_curve = env->fog_depth_curve;
		state.ubo_data.fog_transmit_enabled = env->fog_transmit_enabled;
		state.ubo_data.fog_transmit_curve = env->fog_transmit_curve;
		state.ubo_data.fog_height_enabled = env->fog_height_enabled;
		state.ubo_data.fog_height_min = env->fog_height_min;
		state.ubo_data.fog_height_max = env->fog_height_max;
		state.ubo_data.fog_height_curve = env->fog_height_curve;

	} else {
		state.ubo_data.bg_energy = 1.0;
		state.ubo_data.ambient_energy = 1.0;
		//use from clear color instead, since there is no ambient
		Color linear_ambient_color = storage->frame.clear_request_color.to_linear();
		state.ubo_data.ambient_light_color[0] = linear_ambient_color.r;
		state.ubo_data.ambient_light_color[1] = linear_ambient_color.g;
		state.ubo_data.ambient_light_color[2] = linear_ambient_color.b;
		state.ubo_data.ambient_light_color[3] = linear_ambient_color.a;

		state.ubo_data.bg_color[0] = linear_ambient_color.r;
		state.ubo_data.bg_color[1] = linear_ambient_color.g;
		state.ubo_data.bg_color[2] = linear_ambient_color.b;
		state.ubo_data.bg_color[3] = linear_ambient_color.a;

		state.env_radiance_data.ambient_contribution = 0;
		state.ubo_data.ambient_occlusion_affect_light = 0;

		state.ubo_data.fog_color_enabled[3] = 0.0;
	}

	{
		//directional shadow

		state.ubo_data.shadow_directional_pixel_size[0] = 1.0 / directional_shadow.size;
		state.ubo_data.shadow_directional_pixel_size[1] = 1.0 / directional_shadow.size;

		glActiveTexture(GL_TEXTURE0 + storage->config.max_texture_image_units - 4);
		glBindTexture(GL_TEXTURE_2D, directional_shadow.depth);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LESS);
	}

	glBindBuffer(GL_UNIFORM_BUFFER, state.scene_ubo);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(State::SceneDataUBO), &state.ubo_data);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	//fill up environment

	store_transform(sky_orientation * p_cam_transform, state.env_radiance_data.transform);

	glBindBuffer(GL_UNIFORM_BUFFER, state.env_radiance_ubo);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(State::EnvironmentRadianceUBO), &state.env_radiance_data);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
#endif

	RD::get_singleton()->buffer_update(scene_state.uniform_buffer, 0, sizeof(SceneState::UBO), &scene_state.ubo, true);
}

void RasterizerSceneForwardRD::_add_geometry(InstanceBase *p_instance, uint32_t p_surface, RID p_material, PassMode p_pass_mode, uint32_t p_geometry_index) {

	RID m_src = p_instance->material_override.is_valid() ? p_instance->material_override : p_material;

	/*if (state.debug_draw == VS::VIEWPORT_DEBUG_DRAW_OVERDRAW) {
		m_src = default_overdraw_material;
	}*/

	MaterialData *material = NULL;

	if (m_src.is_valid()) {
		material = (MaterialData *)storage->material_get_data(m_src, RasterizerStorageRD::SHADER_TYPE_3D);
		if (!material || !material->shader_data->valid) {
			material = NULL;
		}
	}

	if (!material) {
		material = (MaterialData *)storage->material_get_data(default_material, RasterizerStorageRD::SHADER_TYPE_3D);
	}

	ERR_FAIL_COND(!material);

	_add_geometry_with_material(p_instance, p_surface, material, p_pass_mode, p_geometry_index);

	while (material->next_pass.is_valid()) {

		material = (MaterialData *)storage->material_get_data(material->next_pass, RasterizerStorageRD::SHADER_TYPE_3D);
		if (!material || !material->shader_data->valid)
			break;
		_add_geometry_with_material(p_instance, p_surface, material, p_pass_mode, p_geometry_index);
	}
}

void RasterizerSceneForwardRD::_add_geometry_with_material(InstanceBase *p_instance, uint32_t p_surface, MaterialData *p_material, PassMode p_pass_mode, uint32_t p_geometry_index) {

	bool has_read_screen_alpha = p_material->shader_data->uses_screen_texture || p_material->shader_data->uses_depth_texture || p_material->shader_data->uses_normal_texture;
	bool has_base_alpha = (p_material->shader_data->uses_alpha || has_read_screen_alpha);
	bool has_blend_alpha = p_material->shader_data->uses_blend_alpha;
	bool has_alpha = has_base_alpha || has_blend_alpha;

	if (p_material->shader_data->uses_sss) {
		scene_state.used_sss = true;
	}

	if (p_material->shader_data->uses_screen_texture) {
		scene_state.used_screen_texture = true;
	}

	if (p_material->shader_data->uses_depth_texture) {
		scene_state.used_depth_texture = true;
	}

	if (p_material->shader_data->uses_normal_texture) {
		scene_state.used_normal_texture = true;
	}

	if (p_pass_mode != PASS_MODE_COLOR && p_pass_mode != PASS_MODE_COLOR_SPECULAR) {

		if (has_blend_alpha || has_read_screen_alpha || (has_base_alpha && !p_material->shader_data->uses_depth_pre_pass) || p_material->shader_data->depth_draw == ShaderData::DEPTH_DRAW_DISABLED || p_material->shader_data->depth_test == ShaderData::DEPTH_TEST_DISABLED || p_instance->cast_shadows == VS::SHADOW_CASTING_SETTING_OFF) {
			//conditions in which no depth pass should be processed
			return;
		}

		if (!p_material->shader_data->writes_modelview_or_projection && !p_material->shader_data->uses_vertex && !p_material->shader_data->uses_discard && !p_material->shader_data->uses_depth_pre_pass) {
			//shader does not use discard and does not write a vertex position, use generic material
			if (p_pass_mode == PASS_MODE_SHADOW || p_pass_mode == PASS_MODE_DEPTH) {
				p_material = (MaterialData *)storage->material_get_data(default_material, RasterizerStorageRD::SHADER_TYPE_3D);
			} else if (p_pass_mode == PASS_MODE_DEPTH_NORMAL && !p_material->shader_data->uses_normal) {
				p_material = (MaterialData *)storage->material_get_data(default_material, RasterizerStorageRD::SHADER_TYPE_3D);
			} else if (p_pass_mode == PASS_MODE_DEPTH_NORMAL_ROUGHNESS && !p_material->shader_data->uses_normal && !p_material->shader_data->uses_roughness) {
				p_material = (MaterialData *)storage->material_get_data(default_material, RasterizerStorageRD::SHADER_TYPE_3D);
			}
		}

		has_alpha = false;
	}

	RenderList::Element *e = (has_alpha || p_material->shader_data->depth_test == ShaderData::DEPTH_TEST_DISABLED) ? render_list.add_alpha_element() : render_list.add_element();

	if (!e)
		return;

	e->instance = p_instance;
	e->material = p_material;
	e->surface_index = p_surface;
	e->sort_key = 0;

	if (e->material->last_pass != render_pass) {
		e->material->last_pass = render_pass;
		e->material->index = scene_state.current_material_index++;
		if (e->material->shader_data->last_pass != render_pass) {
			e->material->shader_data->last_pass = scene_state.current_material_index++;
			e->material->shader_data->index = scene_state.current_shader_index++;
		}
	}
	e->geometry_index = p_geometry_index;
	e->material_index = e->material->index;
	e->uses_instancing = e->instance->base_type == VS::INSTANCE_MULTIMESH;
	e->uses_lightmap = e->instance->lightmap.is_valid();
	e->uses_vct = e->instance->gi_probe_instances.size();
	e->shader_index = e->shader_index;
	e->depth_layer = e->instance->depth_layer;
	e->priority = p_material->priority;

	if (p_material->shader_data->uses_time) {
		VisualServerRaster::redraw_request();
	}
}

void RasterizerSceneForwardRD::_fill_render_list(InstanceBase **p_cull_result, int p_cull_count, PassMode p_pass_mode, bool p_no_gi) {

	scene_state.current_shader_index = 0;
	scene_state.current_material_index = 0;
	scene_state.used_sss = false;
	scene_state.used_screen_texture = false;
	scene_state.used_normal_texture = false;
	scene_state.used_depth_texture = false;

	uint32_t geometry_index = 0;

	//fill list

	for (int i = 0; i < p_cull_count; i++) {

		InstanceBase *inst = p_cull_result[i];

		//add geometry for drawing
		switch (inst->base_type) {

			case VS::INSTANCE_MESH: {

				const RID *materials = NULL;
				uint32_t surface_count;

				materials = storage->mesh_get_surface_count_and_materials(inst->base, surface_count);
				if (!materials) {
					continue; //nothing to do
				}

				const RID *inst_materials = inst->materials.ptr();

				for (uint32_t j = 0; j < surface_count; j++) {

					RID material = inst_materials[j].is_valid() ? inst_materials[j] : materials[j];

					uint32_t surface_index = storage->mesh_surface_get_render_pass_index(inst->base, j, render_pass, &geometry_index);
					_add_geometry(inst, j, material, p_pass_mode, surface_index);
				}

				//mesh->last_pass=frame;

			} break;
#if 0
			case VS::INSTANCE_MULTIMESH: {

				RasterizerStorageGLES3::MultiMesh *multi_mesh = storage->multimesh_owner.getornull(inst->base);
				ERR_CONTINUE(!multi_mesh);

				if (multi_mesh->size == 0 || multi_mesh->visible_instances == 0)
					continue;

				RasterizerStorageGLES3::Mesh *mesh = storage->mesh_owner.getornull(multi_mesh->mesh);
				if (!mesh)
					continue; //mesh not assigned

				int ssize = mesh->surfaces.size();

				for (int j = 0; j < ssize; j++) {

					RasterizerStorageGLES3::Surface *s = mesh->surfaces[j];
					_add_geometry(s, inst, multi_mesh, -1, p_depth_pass, p_shadow_pass);
				}

			} break;
			case VS::INSTANCE_IMMEDIATE: {

				RasterizerStorageGLES3::Immediate *immediate = storage->immediate_owner.getornull(inst->base);
				ERR_CONTINUE(!immediate);

				_add_geometry(immediate, inst, NULL, -1, p_depth_pass, p_shadow_pass);

			} break;
			case VS::INSTANCE_PARTICLES: {

				RasterizerStorageGLES3::Particles *particles = storage->particles_owner.getornull(inst->base);
				ERR_CONTINUE(!particles);

				for (int j = 0; j < particles->draw_passes.size(); j++) {

					RID pmesh = particles->draw_passes[j];
					if (!pmesh.is_valid())
						continue;
					RasterizerStorageGLES3::Mesh *mesh = storage->mesh_owner.getornull(pmesh);
					if (!mesh)
						continue; //mesh not assigned

					int ssize = mesh->surfaces.size();

					for (int k = 0; k < ssize; k++) {

						RasterizerStorageGLES3::Surface *s = mesh->surfaces[k];
						_add_geometry(s, inst, particles, -1, p_depth_pass, p_shadow_pass);
					}
				}

			} break;
#endif
			default: {
			}
		}
	}
}

void RasterizerSceneForwardRD::_draw_sky(RD::DrawListID p_draw_list, RD::FramebufferFormatID p_fb_format, RID p_environment, const CameraMatrix &p_projection, const Transform &p_transform, float p_alpha) {

	ERR_FAIL_COND(!is_environment(p_environment));

	RID sky = environment_get_sky(p_environment);
	ERR_FAIL_COND(!sky.is_valid());
	RID panorama = sky_get_panorama_texture_rd(sky);
	ERR_FAIL_COND(!panorama.is_valid());
	Basis sky_transform = environment_get_sky_orientation(p_environment);
	sky_transform.invert();

	float multiplier = environment_get_bg_energy(p_environment);
	float custom_fov = environment_get_sky_custom_fov(p_environment);
	// Camera
	CameraMatrix camera;

	if (custom_fov) {

		float near_plane = p_projection.get_z_near();
		float far_plane = p_projection.get_z_far();
		float aspect = p_projection.get_aspect();

		camera.set_perspective(custom_fov, aspect, near_plane, far_plane);

	} else {
		camera = p_projection;
	}

	sky_transform = p_transform.basis * sky_transform;
	storage->get_effects()->render_panorama(p_draw_list, p_fb_format, panorama, camera, sky_transform, 1.0, multiplier);
}

void RasterizerSceneForwardRD::_setup_reflections(RID *p_reflection_probe_cull_result, int p_reflection_probe_cull_count, const Transform &p_camera_inverse_transform, RID p_environment) {

	for (int i = 0; i < p_reflection_probe_cull_count; i++) {

		RID rpi = p_reflection_probe_cull_result[i];

		if (i >= (int)scene_state.max_reflections) {
			reflection_probe_instance_set_render_index(rpi, 0); //invalid, but something needs to be set
			continue;
		}

		reflection_probe_instance_set_render_index(rpi, i);

		RID base_probe = reflection_probe_instance_get_probe(rpi);

		ReflectionData &reflection_ubo = scene_state.reflections[i];

		Vector3 extents = storage->reflection_probe_get_extents(base_probe);

		reflection_ubo.box_extents[0] = extents.x;
		reflection_ubo.box_extents[1] = extents.y;
		reflection_ubo.box_extents[2] = extents.z;
		reflection_ubo.index = reflection_probe_instance_get_atlas_index(rpi);

		Vector3 origin_offset = storage->reflection_probe_get_origin_offset(base_probe);

		reflection_ubo.box_offset[0] = origin_offset.x;
		reflection_ubo.box_offset[1] = origin_offset.y;
		reflection_ubo.box_offset[2] = origin_offset.z;
		reflection_ubo.mask = storage->reflection_probe_get_cull_mask(base_probe);

		float intensity = storage->reflection_probe_get_intensity(base_probe);
		bool interior = storage->reflection_probe_is_interior(base_probe);
		bool box_projection = storage->reflection_probe_is_box_projection(base_probe);

		reflection_ubo.params[0] = intensity;
		reflection_ubo.params[1] = 0;
		reflection_ubo.params[2] = interior ? 1.0 : 0.0;
		reflection_ubo.params[3] = box_projection ? 1.0 : 0.0;

		if (interior) {
			Color ambient_linear = storage->reflection_probe_get_interior_ambient(base_probe).to_linear();
			float interior_ambient_energy = storage->reflection_probe_get_interior_ambient_energy(base_probe);
			float interior_ambient_probe_contrib = storage->reflection_probe_get_interior_ambient_probe_contribution(base_probe);
			reflection_ubo.ambient[0] = ambient_linear.r * interior_ambient_energy;
			reflection_ubo.ambient[1] = ambient_linear.g * interior_ambient_energy;
			reflection_ubo.ambient[2] = ambient_linear.b * interior_ambient_energy;
			reflection_ubo.ambient[3] = interior_ambient_probe_contrib;
		} else {
			Color ambient_linear = storage->reflection_probe_get_interior_ambient(base_probe).to_linear();
			if (is_environment(p_environment)) {
				Color env_ambient_color = environment_get_ambient_light_color(p_environment).to_linear();
				float env_ambient_energy = environment_get_ambient_light_ambient_energy(p_environment);
				ambient_linear = env_ambient_color;
				ambient_linear.r *= env_ambient_energy;
				ambient_linear.g *= env_ambient_energy;
				ambient_linear.b *= env_ambient_energy;
			}

			reflection_ubo.ambient[0] = ambient_linear.r;
			reflection_ubo.ambient[1] = ambient_linear.g;
			reflection_ubo.ambient[2] = ambient_linear.b;
			reflection_ubo.ambient[3] = 0; //not used in exterior mode, since it just blends with regular ambient light
		}

		Transform transform = reflection_probe_instance_get_transform(rpi);
		Transform proj = (p_camera_inverse_transform * transform).inverse();
		store_transform(proj, reflection_ubo.local_matrix);

		reflection_probe_instance_set_render_pass(rpi, render_pass);
	}

	if (p_reflection_probe_cull_count) {
		RD::get_singleton()->buffer_update(scene_state.reflection_buffer, 0, MIN(scene_state.max_reflections, p_reflection_probe_cull_count) * sizeof(ReflectionData), scene_state.reflections, true);
	}
}

void RasterizerSceneForwardRD::_setup_lights(RID *p_light_cull_result, int p_light_cull_count, const Transform &p_camera_inverse_transform, RID p_shadow_atlas, bool p_using_shadows) {

	uint32_t light_count = 0;
	scene_state.ubo.directional_light_count = 0;

	for (int i = 0; i < p_light_cull_count; i++) {

		RID li = p_light_cull_result[i];
		RID base = light_instance_get_base_light(li);

		ERR_CONTINUE(base.is_null());

		VS::LightType type = storage->light_get_type(base);
		switch (type) {

			case VS::LIGHT_DIRECTIONAL: {

				if (scene_state.ubo.directional_light_count >= scene_state.max_directional_lights) {
					continue;
				}

				DirectionalLightData &light_data = scene_state.directional_lights[scene_state.ubo.directional_light_count];

				Transform light_transform = light_instance_get_base_transform(li);

				Vector3 direction = p_camera_inverse_transform.basis.xform(light_transform.basis.xform(Vector3(0, 0, 1))).normalized();

				light_data.direction[0] = direction.x;
				light_data.direction[1] = direction.y;
				light_data.direction[2] = direction.z;

				float sign = storage->light_is_negative(base) ? -1 : 1;

				light_data.energy = sign * storage->light_get_param(base, VS::LIGHT_PARAM_ENERGY) * Math_PI;

				Color linear_col = storage->light_get_color(base).to_linear();
				light_data.color[0] = linear_col.r;
				light_data.color[1] = linear_col.g;
				light_data.color[2] = linear_col.b;

				light_data.specular = storage->light_get_param(base, VS::LIGHT_PARAM_SPECULAR);
				light_data.mask = storage->light_get_cull_mask(base);

				Color shadow_col = storage->light_get_shadow_color(base).to_linear();

				light_data.shadow_color[0] = shadow_col.r;
				light_data.shadow_color[1] = shadow_col.g;
				light_data.shadow_color[2] = shadow_col.b;

				light_data.shadow_enabled = p_using_shadows && storage->light_has_shadow(base);

				if (light_data.shadow_enabled) {

					VS::LightDirectionalShadowMode smode = storage->light_directional_get_shadow_mode(base);

					int limit = smode == VS::LIGHT_DIRECTIONAL_SHADOW_ORTHOGONAL ? 0 : (smode == VS::LIGHT_DIRECTIONAL_SHADOW_PARALLEL_2_SPLITS ? 1 : 3);
					light_data.blend_splits = storage->light_directional_get_blend_splits(base);
					for (int j = 0; j < 4; j++) {
						Rect2 atlas_rect = light_instance_get_directional_shadow_atlas_rect(li, j);
						CameraMatrix matrix = light_instance_get_shadow_camera(li, j);
						float split = light_instance_get_directional_shadow_split(li, MIN(limit, j));

						CameraMatrix bias;
						bias.set_light_bias();
						CameraMatrix rectm;
						rectm.set_light_atlas_rect(atlas_rect);

						Transform modelview = (p_camera_inverse_transform * light_instance_get_shadow_transform(li, j)).inverse();

						CameraMatrix shadow_mtx = rectm * bias * matrix * modelview;
						light_data.shadow_split_offsets[j] = split;
						store_camera(shadow_mtx, light_data.shadow_matrices[j]);
					}

					float fade_start = storage->light_get_param(base, VS::LIGHT_PARAM_SHADOW_FADE_START);
					light_data.fade_from = -light_data.shadow_split_offsets[3] * MIN(fade_start, 0.999); //using 1.0 would break smoothstep
					light_data.fade_to = -light_data.shadow_split_offsets[3];
				}

				scene_state.ubo.directional_light_count++;
			} break;
			case VS::LIGHT_SPOT:
			case VS::LIGHT_OMNI: {

				if (light_count >= scene_state.max_lights) {
					continue;
				}

				Transform light_transform = light_instance_get_base_transform(li);

				LightData &light_data = scene_state.lights[light_count];

				float sign = storage->light_is_negative(base) ? -1 : 1;
				Color linear_col = storage->light_get_color(base).to_linear();

				light_data.attenuation_energy[0] = Math::make_half_float(storage->light_get_param(base, VS::LIGHT_PARAM_ATTENUATION));
				light_data.attenuation_energy[1] = Math::make_half_float(sign * storage->light_get_param(base, VS::LIGHT_PARAM_ENERGY) * Math_PI);

				light_data.color_specular[0] = CLAMP(uint32_t(linear_col.r * 255), 0, 255);
				light_data.color_specular[1] = CLAMP(uint32_t(linear_col.g * 255), 0, 255);
				light_data.color_specular[2] = CLAMP(uint32_t(linear_col.b * 255), 0, 255);
				light_data.color_specular[3] = CLAMP(uint32_t(storage->light_get_param(base, VS::LIGHT_PARAM_SPECULAR) * 255), 0, 255);

				light_data.inv_radius = 1.0 / MAX(0.001, storage->light_get_param(base, VS::LIGHT_PARAM_RANGE));

				Vector3 pos = p_camera_inverse_transform.xform(light_transform.origin);

				light_data.position[0] = pos.x;
				light_data.position[1] = pos.y;
				light_data.position[2] = pos.z;

				Vector3 direction = p_camera_inverse_transform.basis.xform(light_transform.basis.xform(Vector3(0, 0, -1))).normalized();

				light_data.direction[0] = direction.x;
				light_data.direction[1] = direction.y;
				light_data.direction[2] = direction.z;

				light_data.cone_attenuation_angle[0] = Math::make_half_float(storage->light_get_param(base, VS::LIGHT_PARAM_SPOT_ATTENUATION));
				light_data.cone_attenuation_angle[1] = Math::make_half_float(Math::cos(Math::deg2rad(storage->light_get_param(base, VS::LIGHT_PARAM_SPOT_ANGLE))));

				light_data.mask = storage->light_get_cull_mask(base);

				Color shadow_color = storage->light_get_shadow_color(base);

				bool has_shadow = p_using_shadows && storage->light_has_shadow(base);
				light_data.shadow_color_enabled[0] = CLAMP(uint32_t(shadow_color.r * 255), 0, 255);
				light_data.shadow_color_enabled[1] = CLAMP(uint32_t(shadow_color.g * 255), 0, 255);
				light_data.shadow_color_enabled[2] = CLAMP(uint32_t(shadow_color.b * 255), 0, 255);
				light_data.shadow_color_enabled[3] = has_shadow ? 255 : 0;

				light_data.atlas_rect[0] = 0;
				light_data.atlas_rect[1] = 0;
				light_data.atlas_rect[2] = 0;
				light_data.atlas_rect[3] = 0;

				if (p_using_shadows && p_shadow_atlas.is_valid() && shadow_atlas_owns_light_instance(p_shadow_atlas, li)) {
					// fill in the shadow information

					Rect2 rect = light_instance_get_shadow_atlas_rect(li, p_shadow_atlas);

					if (type == VS::LIGHT_OMNI) {

						light_data.atlas_rect[0] = rect.position.x;
						light_data.atlas_rect[1] = rect.position.y;
						light_data.atlas_rect[2] = rect.size.width;
						light_data.atlas_rect[3] = rect.size.height * 0.5;

						Transform proj = (p_camera_inverse_transform * light_transform).inverse();

						store_transform(proj, light_data.shadow_matrix);
					} else if (type == VS::LIGHT_SPOT) {

						Transform modelview = (p_camera_inverse_transform * light_transform).inverse();
						CameraMatrix bias;
						bias.set_light_bias();
						CameraMatrix rectm;
						rectm.set_light_atlas_rect(rect);

						CameraMatrix shadow_mtx = rectm * bias * light_instance_get_shadow_camera(li, 0) * modelview;
						store_camera(shadow_mtx, light_data.shadow_matrix);
					}
				}

				light_instance_set_index(li, light_count);

				light_count++;
			} break;
		}

		light_instance_set_render_pass(li, render_pass);

		//update UBO for forward rendering, blit to texture for clustered
	}

	if (light_count) {
		RD::get_singleton()->buffer_update(scene_state.light_buffer, 0, sizeof(LightData) * light_count, scene_state.lights, true);
	}

	if (scene_state.ubo.directional_light_count) {
		RD::get_singleton()->buffer_update(scene_state.directional_light_buffer, 0, sizeof(DirectionalLightData) * scene_state.ubo.directional_light_count, scene_state.directional_lights, true);
	}
}

void RasterizerSceneForwardRD::_render_scene(RenderBufferData *p_buffer_data, const Transform &p_cam_transform, const CameraMatrix &p_cam_projection, bool p_cam_ortogonal, InstanceBase **p_cull_result, int p_cull_count, RID *p_light_cull_result, int p_light_cull_count, RID *p_reflection_probe_cull_result, int p_reflection_probe_cull_count, RID p_environment, RID p_shadow_atlas, RID p_reflection_atlas, RID p_reflection_probe, int p_reflection_probe_pass) {

	RenderBufferDataForward *render_buffer = (RenderBufferDataForward *)p_buffer_data;

	//first of all, make a new render pass
	render_pass++;

	//fill up ubo
#if 0
	storage->info.render.object_count += p_cull_count;

	Environment *env = environment_owner.getornull(p_environment);
	ShadowAtlas *shadow_atlas = shadow_atlas_owner.getornull(p_shadow_atlas);
	ReflectionAtlas *reflection_atlas = reflection_atlas_owner.getornull(p_reflection_atlas);

	if (shadow_atlas && shadow_atlas->size) {
		glActiveTexture(GL_TEXTURE0 + storage->config.max_texture_image_units - 5);
		glBindTexture(GL_TEXTURE_2D, shadow_atlas->depth);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LESS);
		scene_state.ubo.shadow_atlas_pixel_size[0] = 1.0 / shadow_atlas->size;
		scene_state.ubo.shadow_atlas_pixel_size[1] = 1.0 / shadow_atlas->size;
	}

	if (reflection_atlas && reflection_atlas->size) {
		glActiveTexture(GL_TEXTURE0 + storage->config.max_texture_image_units - 3);
		glBindTexture(GL_TEXTURE_2D, reflection_atlas->color);
	}
#endif

	bool using_shadows = true;

	if (p_reflection_probe.is_valid()) {
		scene_state.ubo.reflection_multiplier = 0.0;
		if (!storage->reflection_probe_renders_shadows(reflection_probe_instance_get_probe(p_reflection_probe))) {
			using_shadows = false;
		}
	} else {
		scene_state.ubo.reflection_multiplier = 1.0;
	}

	//scene_state.ubo.subsurface_scatter_width = subsurface_scatter_size;

	scene_state.ubo.shadow_z_offset = 0;
	scene_state.ubo.shadow_z_slope_scale = 0;

	p_cam_projection.get_viewport_size(scene_state.ubo.viewport_size[0], scene_state.ubo.viewport_size[1]);

	RID render_target;
	Size2 screen_pixel_size;
	RID opaque_framebuffer;
	RID alpha_framebuffer;

	if (render_buffer) {
		screen_pixel_size.width = 1.0 / render_buffer->width;
		screen_pixel_size.height = 1.0 / render_buffer->height;
		render_target = render_buffer->render_target;

		opaque_framebuffer = render_buffer->color_fb;
		alpha_framebuffer = opaque_framebuffer;

	} else if (p_reflection_probe.is_valid()) {
		uint32_t resolution = reflection_probe_instance_get_resolution(p_reflection_probe);
		screen_pixel_size.width = 1.0 / resolution;
		screen_pixel_size.height = 1.0 / resolution;

		opaque_framebuffer = reflection_probe_instance_get_framebuffer(p_reflection_probe, p_reflection_probe_pass);
		alpha_framebuffer = opaque_framebuffer;

		if (storage->reflection_probe_is_interior(reflection_probe_instance_get_probe(p_reflection_probe))) {
			p_environment = RID(); //no environment on interiors
		}

	} else {
		ERR_FAIL(); //bug?
	}

	_setup_lights(p_light_cull_result, p_light_cull_count, p_cam_transform.affine_inverse(), p_shadow_atlas, using_shadows);
	_setup_reflections(p_reflection_probe_cull_result, p_reflection_probe_cull_count, p_cam_transform.affine_inverse(), p_environment);
	_setup_environment(render_target, p_environment, p_cam_projection, p_cam_transform, p_reflection_probe, p_reflection_probe.is_valid(), screen_pixel_size, p_shadow_atlas);

#if 0
	for (int i = 0; i < p_light_cull_count; i++) {

		ERR_BREAK(i >= RenderList::MAX_LIGHTS);

		LightInstance *li = light_instance_owner.getornull(p_light_cull_result[i]);
		if (li->light_ptr->param[VS::LIGHT_PARAM_CONTACT_SHADOW_SIZE] > CMP_EPSILON) {
			state.used_contact_shadows = true;
		}
	}
#endif
#if 0
	// Do depth prepass if it's explicitly enabled
	bool use_depth_prepass = storage->config.use_depth_prepass;

	// If contact shadows are used then we need to do depth prepass even if it's otherwise disabled
	use_depth_prepass = use_depth_prepass || state.used_contact_shadows;

	// Never do depth prepass if effects are disabled or if we render overdraws
	use_depth_prepass = use_depth_prepass && storage->frame.current_rt && !storage->frame.current_rt->flags[RasterizerStorage::RENDER_TARGET_NO_3D_EFFECTS];
	use_depth_prepass = use_depth_prepass && state.debug_draw != VS::VIEWPORT_DEBUG_DRAW_OVERDRAW;

	if (use_depth_prepass) {
		//pre z pass

		glDisable(GL_BLEND);
		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_SCISSOR_TEST);
		glBindFramebuffer(GL_FRAMEBUFFER, storage->frame.current_rt->buffers.fbo);
		glDrawBuffers(0, NULL);

		glViewport(0, 0, storage->frame.current_rt->width, storage->frame.current_rt->height);

		glColorMask(0, 0, 0, 0);
		glClearDepth(1.0f);
		glClear(GL_DEPTH_BUFFER_BIT);

		render_list.clear();
		_fill_render_list(p_cull_result, p_cull_count, true, false);
		render_list.sort_by_key(false);
		state.scene_shader.set_conditional(SceneShaderGLES3::RENDER_DEPTH, true);
		_render_list(render_list.elements, render_list.element_count, p_cam_transform, p_cam_projection, 0, false, false, true, false, false);
		state.scene_shader.set_conditional(SceneShaderGLES3::RENDER_DEPTH, false);

		glColorMask(1, 1, 1, 1);

		if (state.used_contact_shadows) {

			_prepare_depth_texture();
			_bind_depth_texture();
		}

		fb_cleared = true;
		render_pass++;
		state.used_depth_prepass = true;
	} else {
		state.used_depth_prepass = false;
	}

	_setup_lights(p_light_cull_result, p_light_cull_count, p_cam_transform.affine_inverse(), p_cam_projection, p_shadow_atlas);
	_setup_reflections(p_reflection_probe_cull_result, p_reflection_probe_cull_count, p_cam_transform.affine_inverse(), p_cam_projection, p_reflection_atlas, env);

	bool use_mrt = false;
#endif

	render_list.clear();
	_fill_render_list(p_cull_result, p_cull_count, PASS_MODE_COLOR, render_buffer == nullptr);
#if 0
	//

	glEnable(GL_BLEND);
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);

	//rendering to a probe cubemap side
	ReflectionProbeInstance *probe = reflection_probe_instance_owner.getornull(p_reflection_probe);
	GLuint current_fbo;

	if (probe) {

		ReflectionAtlas *ref_atlas = reflection_atlas_owner.getornull(probe->atlas);
		ERR_FAIL_COND(!ref_atlas);

		int target_size = ref_atlas->size / ref_atlas->subdiv;

		int cubemap_index = reflection_cubemaps.size() - 1;

		for (int i = reflection_cubemaps.size() - 1; i >= 0; i--) {
			//find appropriate cubemap to render to
			if (reflection_cubemaps[i].size > target_size * 2)
				break;

			cubemap_index = i;
		}

		current_fbo = reflection_cubemaps[cubemap_index].fbo_id[p_reflection_probe_pass];
		use_mrt = false;
		state.scene_shader.set_conditional(SceneShaderGLES3::USE_MULTIPLE_RENDER_TARGETS, false);

		glViewport(0, 0, reflection_cubemaps[cubemap_index].size, reflection_cubemaps[cubemap_index].size);
		glBindFramebuffer(GL_FRAMEBUFFER, current_fbo);

	} else {

		use_mrt = env && (state.used_sss || env->ssao_enabled || env->ssr_enabled || env->dof_blur_far_enabled || env->dof_blur_near_enabled); //only enable MRT rendering if any of these is enabled
		//effects disabled and transparency also prevent using MRTs
		use_mrt = use_mrt && !storage->frame.current_rt->flags[RasterizerStorage::RENDER_TARGET_TRANSPARENT];
		use_mrt = use_mrt && !storage->frame.current_rt->flags[RasterizerStorage::RENDER_TARGET_NO_3D_EFFECTS];
		use_mrt = use_mrt && state.debug_draw != VS::VIEWPORT_DEBUG_DRAW_OVERDRAW;
		use_mrt = use_mrt && (env->bg_mode != VS::ENV_BG_KEEP && env->bg_mode != VS::ENV_BG_CANVAS);

		glViewport(0, 0, storage->frame.current_rt->width, storage->frame.current_rt->height);

		if (use_mrt) {

			current_fbo = storage->frame.current_rt->buffers.fbo;

			glBindFramebuffer(GL_FRAMEBUFFER, storage->frame.current_rt->buffers.fbo);
			state.scene_shader.set_conditional(SceneShaderGLES3::USE_MULTIPLE_RENDER_TARGETS, true);

			Vector<GLenum> draw_buffers;
			draw_buffers.push_back(GL_COLOR_ATTACHMENT0);
			draw_buffers.push_back(GL_COLOR_ATTACHMENT1);
			draw_buffers.push_back(GL_COLOR_ATTACHMENT2);
			if (state.used_sss) {
				draw_buffers.push_back(GL_COLOR_ATTACHMENT3);
			}
			glDrawBuffers(draw_buffers.size(), draw_buffers.ptr());

			Color black(0, 0, 0, 0);
			glClearBufferfv(GL_COLOR, 1, black.components); // specular
			glClearBufferfv(GL_COLOR, 2, black.components); // normal metal rough
			if (state.used_sss) {
				glClearBufferfv(GL_COLOR, 3, black.components); // normal metal rough
			}

		} else {

			if (storage->frame.current_rt->buffers.active) {
				current_fbo = storage->frame.current_rt->buffers.fbo;
			} else {
				current_fbo = storage->frame.current_rt->effects.mip_maps[0].sizes[0].fbo;
			}

			glBindFramebuffer(GL_FRAMEBUFFER, current_fbo);
			state.scene_shader.set_conditional(SceneShaderGLES3::USE_MULTIPLE_RENDER_TARGETS, false);

			Vector<GLenum> draw_buffers;
			draw_buffers.push_back(GL_COLOR_ATTACHMENT0);
			glDrawBuffers(draw_buffers.size(), draw_buffers.ptr());
		}
	}

	if (!fb_cleared) {
		glClearDepth(1.0f);
		glClear(GL_DEPTH_BUFFER_BIT);
	}

	Color clear_color(0, 0, 0, 0);

	RasterizerStorageGLES3::Sky *sky = NULL;
	Ref<CameraFeed> feed;
	GLuint env_radiance_tex = 0;

	if (state.debug_draw == VS::VIEWPORT_DEBUG_DRAW_OVERDRAW) {
		clear_color = Color(0, 0, 0, 0);
		storage->frame.clear_request = false;
	} else if (!probe && storage->frame.current_rt->flags[RasterizerStorage::RENDER_TARGET_TRANSPARENT]) {
		clear_color = Color(0, 0, 0, 0);
		storage->frame.clear_request = false;

	} else if (!env || env->bg_mode == VS::ENV_BG_CLEAR_COLOR) {

		if (storage->frame.clear_request) {

			clear_color = storage->frame.clear_request_color.to_linear();
			storage->frame.clear_request = false;
		}

	} else if (env->bg_mode == VS::ENV_BG_CANVAS) {

		clear_color = env->bg_color.to_linear();
		storage->frame.clear_request = false;
	} else if (env->bg_mode == VS::ENV_BG_COLOR) {

		clear_color = env->bg_color.to_linear();
		storage->frame.clear_request = false;
	} else if (env->bg_mode == VS::ENV_BG_SKY) {

		storage->frame.clear_request = false;

	} else if (env->bg_mode == VS::ENV_BG_COLOR_SKY) {

		clear_color = env->bg_color.to_linear();
		storage->frame.clear_request = false;

	} else if (env->bg_mode == VS::ENV_BG_CAMERA_FEED) {
		feed = CameraServer::get_singleton()->get_feed_by_id(env->camera_feed_id);
		storage->frame.clear_request = false;
	} else {
		storage->frame.clear_request = false;
	}

	if (!env || env->bg_mode != VS::ENV_BG_KEEP) {
		glClearBufferfv(GL_COLOR, 0, clear_color.components); // specular
	}

	VS::EnvironmentBG bg_mode = (!env || (probe && env->bg_mode == VS::ENV_BG_CANVAS)) ? VS::ENV_BG_CLEAR_COLOR : env->bg_mode; //if no environment, or canvas while rendering a probe (invalid use case), use color.

	if (env) {
		switch (bg_mode) {
			case VS::ENV_BG_COLOR_SKY:
			case VS::ENV_BG_SKY:

				sky = storage->sky_owner.getornull(env->sky);

				if (sky) {
					env_radiance_tex = sky->radiance;
				}
				break;
			case VS::ENV_BG_CANVAS:
				//copy canvas to 3d buffer and convert it to linear

				glDisable(GL_BLEND);
				glDepthMask(GL_FALSE);
				glDisable(GL_DEPTH_TEST);
				glDisable(GL_CULL_FACE);

				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, storage->frame.current_rt->color);

				storage->shaders.copy.set_conditional(CopyShaderGLES3::DISABLE_ALPHA, true);

				storage->shaders.copy.set_conditional(CopyShaderGLES3::SRGB_TO_LINEAR, true);

				storage->shaders.copy.bind();

				_copy_screen(true, true);

				//turn off everything used
				storage->shaders.copy.set_conditional(CopyShaderGLES3::SRGB_TO_LINEAR, false);
				storage->shaders.copy.set_conditional(CopyShaderGLES3::DISABLE_ALPHA, false);

				//restore
				glEnable(GL_BLEND);
				glDepthMask(GL_TRUE);
				glEnable(GL_DEPTH_TEST);
				glEnable(GL_CULL_FACE);
				break;
			case VS::ENV_BG_CAMERA_FEED:
				if (feed.is_valid() && (feed->get_base_width() > 0) && (feed->get_base_height() > 0)) {
					// copy our camera feed to our background

					glDisable(GL_BLEND);
					glDepthMask(GL_FALSE);
					glDisable(GL_DEPTH_TEST);
					glDisable(GL_CULL_FACE);

					storage->shaders.copy.set_conditional(CopyShaderGLES3::USE_DISPLAY_TRANSFORM, true);
					storage->shaders.copy.set_conditional(CopyShaderGLES3::DISABLE_ALPHA, true);
					storage->shaders.copy.set_conditional(CopyShaderGLES3::SRGB_TO_LINEAR, true);

					if (feed->get_datatype() == CameraFeed::FEED_RGB) {
						RID camera_RGBA = feed->get_texture(CameraServer::FEED_RGBA_IMAGE);

						VS::get_singleton()->texture_bind(camera_RGBA, 0);
					} else if (feed->get_datatype() == CameraFeed::FEED_YCBCR) {
						RID camera_YCbCr = feed->get_texture(CameraServer::FEED_YCBCR_IMAGE);

						VS::get_singleton()->texture_bind(camera_YCbCr, 0);

						storage->shaders.copy.set_conditional(CopyShaderGLES3::YCBCR_TO_SRGB, true);

					} else if (feed->get_datatype() == CameraFeed::FEED_YCBCR_SEP) {
						RID camera_Y = feed->get_texture(CameraServer::FEED_Y_IMAGE);
						RID camera_CbCr = feed->get_texture(CameraServer::FEED_CBCR_IMAGE);

						VS::get_singleton()->texture_bind(camera_Y, 0);
						VS::get_singleton()->texture_bind(camera_CbCr, 1);

						storage->shaders.copy.set_conditional(CopyShaderGLES3::SEP_CBCR_TEXTURE, true);
						storage->shaders.copy.set_conditional(CopyShaderGLES3::YCBCR_TO_SRGB, true);
					};

					storage->shaders.copy.bind();
					storage->shaders.copy.set_uniform(CopyShaderGLES3::DISPLAY_TRANSFORM, feed->get_transform());

					_copy_screen(true, true);

					//turn off everything used
					storage->shaders.copy.set_conditional(CopyShaderGLES3::USE_DISPLAY_TRANSFORM, false);
					storage->shaders.copy.set_conditional(CopyShaderGLES3::DISABLE_ALPHA, false);
					storage->shaders.copy.set_conditional(CopyShaderGLES3::SRGB_TO_LINEAR, false);
					storage->shaders.copy.set_conditional(CopyShaderGLES3::SEP_CBCR_TEXTURE, false);
					storage->shaders.copy.set_conditional(CopyShaderGLES3::YCBCR_TO_SRGB, false);

					//restore
					glEnable(GL_BLEND);
					glDepthMask(GL_TRUE);
					glEnable(GL_DEPTH_TEST);
					glEnable(GL_CULL_FACE);
				} else {
					// don't have a feed, just show greenscreen :)
					clear_color = Color(0.0, 1.0, 0.0, 1.0);
				}
				break;
			default: {
			}
		}
	}

	if (probe && probe->probe_ptr->interior) {
		env_radiance_tex = 0; //for rendering probe interiors, radiance must not be used.
	}

	state.texscreen_copied = false;

	glBlendEquation(GL_FUNC_ADD);

	if (storage->frame.current_rt && storage->frame.current_rt->flags[RasterizerStorage::RENDER_TARGET_TRANSPARENT]) {
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_BLEND);
	} else {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_BLEND);
	}
#endif

	RID radiance_cubemap;
	bool draw_sky = false;

	Color clear_color;
	bool keep_color = false;

	if (is_environment(p_environment)) {
		VS::EnvironmentBG bg_mode = environment_get_background(p_environment);
		float bg_energy = environment_get_bg_energy(p_environment);
		switch (bg_mode) {
			case VS::ENV_BG_CLEAR_COLOR: {
				clear_color = render_target.is_valid() ? storage->render_target_get_clear_request_color(render_target) : environment_get_bg_color(p_environment);
				clear_color.r *= bg_energy;
				clear_color.g *= bg_energy;
				clear_color.b *= bg_energy;
			} break;
			case VS::ENV_BG_COLOR: {
				clear_color = environment_get_bg_color(p_environment);
				clear_color.r *= bg_energy;
				clear_color.g *= bg_energy;
				clear_color.b *= bg_energy;
			} break;
			case VS::ENV_BG_SKY: {
				RID sky = environment_get_sky(p_environment);
				if (sky.is_valid()) {
					radiance_cubemap = sky_get_radiance_texture_rd(sky);
					draw_sky = true;
				}
			} break;
			case VS::ENV_BG_CANVAS: {
				keep_color = true;
			} break;
			case VS::ENV_BG_KEEP: {
				keep_color = true;
			} break;
			case VS::ENV_BG_CAMERA_FEED: {

			} break;
		}
	} else {

		if (p_reflection_probe.is_valid() && !storage->reflection_probe_is_interior(reflection_probe_instance_get_probe(p_reflection_probe))) {
			clear_color = storage->get_default_clear_color();
		} else if (render_target.is_valid()) {
			clear_color = storage->render_target_get_clear_request_color(render_target);
		}
	}

	_setup_render_base_uniform_set(RID(), RID(), RID(), RID(), radiance_cubemap, p_shadow_atlas, p_reflection_atlas);

	render_list.sort_by_key(false);

	_fill_instances(render_list.elements, render_list.element_count);

	bool can_continue = true; //unless the middle buffers are needed
	bool using_separate_specular = false;

	{
		//regular forward for now
		Vector<Color> c;
		c.push_back(clear_color.to_linear());
		RD::DrawListID draw_list = RD::get_singleton()->draw_list_begin(opaque_framebuffer, keep_color ? RD::INITIAL_ACTION_KEEP_COLOR : RD::INITIAL_ACTION_CLEAR, (can_continue || draw_sky) ? RD::FINAL_ACTION_CONTINUE : RD::FINAL_ACTION_READ_COLOR_AND_DEPTH, c);
		_render_list(draw_list, RD::get_singleton()->framebuffer_get_format(opaque_framebuffer), render_list.elements, render_list.element_count, false, PASS_MODE_COLOR, render_buffer == nullptr);
		RD::get_singleton()->draw_list_end();
	}

	if (draw_sky) {
		RD::DrawListID draw_list = RD::get_singleton()->draw_list_begin(opaque_framebuffer, RD::INITIAL_ACTION_CONTINUE, can_continue ? RD::FINAL_ACTION_CONTINUE : RD::FINAL_ACTION_READ_COLOR_AND_DEPTH);
		_draw_sky(draw_list, RD::get_singleton()->framebuffer_get_format(opaque_framebuffer), p_environment, p_cam_projection, p_cam_transform, 1.0);
		RD::get_singleton()->draw_list_end();

		if (using_separate_specular && !can_continue) {
			//can't continue, so close the buffers
			//RD::get_singleton()->draw_list_begin(render_buffer->color_specular_fb, RD::INITIAL_ACTION_CONTINUE, RD::FINAL_ACTION_READ_COLOR_AND_DEPTH, c);
			//RD::get_singleton()->draw_list_end();
		}
	}

	//_render_list
#if 0
	if (state.directional_light_count == 0) {
		directional_light = NULL;
		_render_list(render_list.elements, render_list.element_count, p_cam_transform, p_cam_projection, env_radiance_tex, false, false, false, false, shadow_atlas != NULL);
	} else {
		for (int i = 0; i < state.directional_light_count; i++) {
			directional_light = directional_lights[i];
			if (i > 0) {
				glEnable(GL_BLEND);
			}
			_setup_directional_light(i, p_cam_transform.affine_inverse(), shadow_atlas != NULL && shadow_atlas->size > 0);
			_render_list(render_list.elements, render_list.element_count, p_cam_transform, p_cam_projection, env_radiance_tex, false, false, false, i > 0, shadow_atlas != NULL);
		}
	}

	state.scene_shader.set_conditional(SceneShaderGLES3::USE_MULTIPLE_RENDER_TARGETS, false);

	if (use_mrt) {
		GLenum gldb = GL_COLOR_ATTACHMENT0;
		glDrawBuffers(1, &gldb);
	}

	if (env && env->bg_mode == VS::ENV_BG_SKY && (!storage->frame.current_rt || (!storage->frame.current_rt->flags[RasterizerStorage::RENDER_TARGET_TRANSPARENT] && state.debug_draw != VS::VIEWPORT_DEBUG_DRAW_OVERDRAW))) {

		/*
		if (use_mrt) {
			glBindFramebuffer(GL_FRAMEBUFFER,storage->frame.current_rt->buffers.fbo); //switch to alpha fbo for sky, only diffuse/ambient matters
		*/

		if (sky && sky->panorama.is_valid())
			_draw_sky(sky, p_cam_projection, p_cam_transform, false, env->sky_custom_fov, env->bg_energy, env->sky_orientation);
	}

	//_render_list_forward(&alpha_render_list,camera_transform,camera_transform_inverse,camera_projection,false,fragment_lighting,true);
	//glColorMask(1,1,1,1);

	//state.scene_shader.set_conditional( SceneShaderGLES3::USE_FOG,false);

	if (use_mrt) {

		_render_mrts(env, p_cam_projection);
	} else {
		// Here we have to do the blits/resolves that otherwise are done in the MRT rendering, in particular
		// - prepare screen texture for any geometry that uses a shader with screen texture
		// - prepare depth texture for any geometry that uses a shader with depth texture

		bool framebuffer_dirty = false;

		if (storage->frame.current_rt && storage->frame.current_rt->buffers.active && state.used_screen_texture) {
			glBindFramebuffer(GL_READ_FRAMEBUFFER, storage->frame.current_rt->buffers.fbo);
			glReadBuffer(GL_COLOR_ATTACHMENT0);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, storage->frame.current_rt->effects.mip_maps[0].sizes[0].fbo);
			glBlitFramebuffer(0, 0, storage->frame.current_rt->width, storage->frame.current_rt->height, 0, 0, storage->frame.current_rt->width, storage->frame.current_rt->height, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			_blur_effect_buffer();
			framebuffer_dirty = true;
		}

		if (storage->frame.current_rt && storage->frame.current_rt->buffers.active && state.used_depth_texture) {
			_prepare_depth_texture();
			framebuffer_dirty = true;
		}

		if (framebuffer_dirty) {
			// Restore framebuffer
			glBindFramebuffer(GL_FRAMEBUFFER, storage->frame.current_rt->buffers.fbo);
			glViewport(0, 0, storage->frame.current_rt->width, storage->frame.current_rt->height);
		}
	}

	if (storage->frame.current_rt && state.used_depth_texture && storage->frame.current_rt->buffers.active) {
		_bind_depth_texture();
	}

	if (storage->frame.current_rt && state.used_screen_texture && storage->frame.current_rt->buffers.active) {
		glActiveTexture(GL_TEXTURE0 + storage->config.max_texture_image_units - 7);
		glBindTexture(GL_TEXTURE_2D, storage->frame.current_rt->effects.mip_maps[0].color);
	}

	glEnable(GL_BLEND);
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
#endif
	render_list.sort_by_reverse_depth_and_priority(true);

	_fill_instances(&render_list.elements[render_list.max_elements - render_list.alpha_element_count], render_list.alpha_element_count);

	{
		RD::DrawListID draw_list = RD::get_singleton()->draw_list_begin(alpha_framebuffer, can_continue ? RD::INITIAL_ACTION_CONTINUE : RD::INITIAL_ACTION_KEEP_COLOR_AND_DEPTH, RD::FINAL_ACTION_READ_COLOR_AND_DEPTH);
		_render_list(draw_list, RD::get_singleton()->framebuffer_get_format(alpha_framebuffer), &render_list.elements[render_list.max_elements - render_list.alpha_element_count], render_list.alpha_element_count, false, PASS_MODE_COLOR, render_buffer == nullptr);
		RD::get_singleton()->draw_list_end();
	}

	//_render_list
#if 0
	if (state.directional_light_count == 0) {
		directional_light = NULL;
		_render_list(&render_list.elements[render_list.max_elements - render_list.alpha_element_count], render_list.alpha_element_count, p_cam_transform, p_cam_projection, env_radiance_tex, false, true, false, false, shadow_atlas != NULL);
	} else {
		for (int i = 0; i < state.directional_light_count; i++) {
			directional_light = directional_lights[i];
			_setup_directional_light(i, p_cam_transform.affine_inverse(), shadow_atlas != NULL && shadow_atlas->size > 0);
			_render_list(&render_list.elements[render_list.max_elements - render_list.alpha_element_count], render_list.alpha_element_count, p_cam_transform, p_cam_projection, env_radiance_tex, false, true, false, i > 0, shadow_atlas != NULL);
		}
	}
#endif
	if (p_reflection_probe.is_valid()) {
		//was rendering a probe, so do no more
		return;
	}

	RasterizerEffectsRD *effects = storage->get_effects();

	{
		//tonemap
		RasterizerEffectsRD::TonemapSettings tonemap;

		tonemap.color_correction_texture = storage->texture_rd_get_default(RasterizerStorageRD::DEFAULT_RD_TEXTURE_3D_WHITE);
		tonemap.exposure_texture = storage->texture_rd_get_default(RasterizerStorageRD::DEFAULT_RD_TEXTURE_WHITE);
		tonemap.glow_texture = storage->texture_rd_get_default(RasterizerStorageRD::DEFAULT_RD_TEXTURE_BLACK);

		if (is_environment(p_environment)) {
			tonemap.tonemap_mode = environment_get_tonemapper(p_environment);
			tonemap.white = environment_get_white(p_environment);
			tonemap.exposure = environment_get_exposure(p_environment);
		}
		effects->tonemapper(render_buffer->color, storage->render_target_get_rd_framebuffer(render_buffer->render_target), tonemap);
	}

	storage->render_target_disable_clear_request(render_buffer->render_target);

	if (false) {
		if (p_shadow_atlas.is_valid()) {
			RID shadow_atlas_texture = shadow_atlas_get_texture(p_shadow_atlas);
			Size2 rtsize = storage->render_target_get_size(render_buffer->render_target);

			effects->copy_to_rect(shadow_atlas_texture, storage->render_target_get_rd_framebuffer(render_buffer->render_target), Rect2(Vector2(), rtsize / 2));
		}
	}

	if (false) {
		if (directional_shadow_get_texture().is_valid()) {
			RID shadow_atlas_texture = directional_shadow_get_texture();
			Size2 rtsize = storage->render_target_get_size(render_buffer->render_target);

			effects->copy_to_rect(shadow_atlas_texture, storage->render_target_get_rd_framebuffer(render_buffer->render_target), Rect2(Vector2(), rtsize / 2));
		}
	}

#if 0
	_post_process(env, p_cam_projection);
	// Needed only for debugging
	/*	if (shadow_atlas && storage->frame.current_rt) {

		//_copy_texture_to_front_buffer(shadow_atlas->depth);
		storage->canvas->canvas_begin();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, shadow_atlas->depth);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
		storage->canvas->draw_generic_textured_rect(Rect2(0, 0, storage->frame.current_rt->width / 2, storage->frame.current_rt->height / 2), Rect2(0, 0, 1, 1));
	}

	if (storage->frame.current_rt) {

		//_copy_texture_to_front_buffer(shadow_atlas->depth);
		storage->canvas->canvas_begin();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, exposure_shrink[4].color);
		//glBindTexture(GL_TEXTURE_2D,storage->frame.current_rt->exposure.color);
		storage->canvas->draw_generic_textured_rect(Rect2(0, 0, storage->frame.current_rt->width / 16, storage->frame.current_rt->height / 16), Rect2(0, 0, 1, 1));
	}

	if (reflection_atlas && storage->frame.current_rt) {

		//_copy_texture_to_front_buffer(shadow_atlas->depth);
		storage->canvas->canvas_begin();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, reflection_atlas->color);
		storage->canvas->draw_generic_textured_rect(Rect2(0, 0, storage->frame.current_rt->width / 2, storage->frame.current_rt->height / 2), Rect2(0, 0, 1, 1));
	}

	if (directional_shadow.fbo) {

		//_copy_texture_to_front_buffer(shadow_atlas->depth);
		storage->canvas->canvas_begin();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, directional_shadow.depth);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
		storage->canvas->draw_generic_textured_rect(Rect2(0, 0, storage->frame.current_rt->width / 2, storage->frame.current_rt->height / 2), Rect2(0, 0, 1, 1));
	}

	if ( env_radiance_tex) {

		//_copy_texture_to_front_buffer(shadow_atlas->depth);
		storage->canvas->canvas_begin();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, env_radiance_tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		storage->canvas->draw_generic_textured_rect(Rect2(0, 0, storage->frame.current_rt->width / 2, storage->frame.current_rt->height / 2), Rect2(0, 0, 1, 1));
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}*/
	//disable all stuff
#endif
}
void RasterizerSceneForwardRD::_render_shadow(RID p_framebuffer, InstanceBase **p_cull_result, int p_cull_count, const CameraMatrix &p_projection, const Transform &p_transform, float p_zfar, float p_bias, float p_normal_bias, bool p_use_dp, bool p_use_dp_flip) {

	render_pass++;

	scene_state.ubo.shadow_z_offset = p_bias;
	scene_state.ubo.shadow_z_slope_scale = p_normal_bias;
	scene_state.ubo.z_far = p_zfar;
	scene_state.ubo.dual_paraboloid_side = p_use_dp_flip ? -1 : 1;

	_setup_environment(RID(), RID(), p_projection, p_transform, RID(), true, Vector2(1, 1), RID());

	render_list.clear();

	PassMode pass_mode = p_use_dp ? PASS_MODE_SHADOW_DP : PASS_MODE_SHADOW;

	_fill_render_list(p_cull_result, p_cull_count, pass_mode, true);

	_setup_render_base_uniform_set(RID(), RID(), RID(), RID(), RID(), RID(), RID());

	render_list.sort_by_key(false);

	_fill_instances(render_list.elements, render_list.element_count);

	{
		//regular forward for now
		RD::DrawListID draw_list = RD::get_singleton()->draw_list_begin(p_framebuffer, RD::INITIAL_ACTION_CLEAR, RD::FINAL_ACTION_READ_COLOR_AND_DEPTH);
		_render_list(draw_list, RD::get_singleton()->framebuffer_get_format(p_framebuffer), render_list.elements, render_list.element_count, p_use_dp_flip, pass_mode, true);
		RD::get_singleton()->draw_list_end();
	}
}

void RasterizerSceneForwardRD::_setup_render_base_uniform_set(RID p_depth_buffer, RID p_color_buffer, RID p_normal_buffer, RID p_roughness_limit_buffer, RID p_radiance_cubemap, RID p_shadow_atlas, RID p_reflection_atlas) {

	if (render_base_uniform_set.is_valid() && RD::get_singleton()->uniform_set_is_valid(render_base_uniform_set)) {
		RD::get_singleton()->free(render_base_uniform_set);
	}

	//default render buffer and scene state uniform set

	Vector<RD::Uniform> uniforms;
	{
		RD::Uniform u;
		u.binding = 1;
		u.type = RD::UNIFORM_TYPE_TEXTURE;
		RID texture = p_depth_buffer.is_valid() ? p_depth_buffer : storage->texture_rd_get_default(RasterizerStorageRD::DEFAULT_RD_TEXTURE_WHITE);
		u.ids.push_back(texture);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.binding = 2;
		u.type = RD::UNIFORM_TYPE_TEXTURE;
		RID texture = p_color_buffer.is_valid() ? p_color_buffer : storage->texture_rd_get_default(RasterizerStorageRD::DEFAULT_RD_TEXTURE_BLACK);
		u.ids.push_back(texture);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.binding = 3;
		u.type = RD::UNIFORM_TYPE_TEXTURE;
		RID texture = p_normal_buffer.is_valid() ? p_normal_buffer : storage->texture_rd_get_default(RasterizerStorageRD::DEFAULT_RD_TEXTURE_NORMAL);
		u.ids.push_back(texture);
		uniforms.push_back(u);
	}

	{
		RD::Uniform u;
		u.binding = 4;
		u.type = RD::UNIFORM_TYPE_TEXTURE;
		RID texture = p_roughness_limit_buffer.is_valid() ? p_roughness_limit_buffer : storage->texture_rd_get_default(RasterizerStorageRD::DEFAULT_RD_TEXTURE_BLACK);
		u.ids.push_back(texture);
		uniforms.push_back(u);
	}

	{
		RD::Uniform u;
		u.binding = 5;
		u.type = RD::UNIFORM_TYPE_TEXTURE;
		RID texture = p_radiance_cubemap.is_valid() ? p_radiance_cubemap : storage->texture_rd_get_default(is_using_radiance_cubemap_array() ? RasterizerStorageRD::DEFAULT_RD_TEXTURE_CUBEMAP_ARRAY_BLACK : RasterizerStorageRD::DEFAULT_RD_TEXTURE_CUBEMAP_BLACK);
		u.ids.push_back(texture);
		uniforms.push_back(u);
	}

	{
		RD::Uniform u;
		u.type = RD::UNIFORM_TYPE_SAMPLER;
		u.binding = 6;
		u.ids.resize(12);
		RID *ids_ptr = u.ids.ptrw();
		ids_ptr[0] = storage->sampler_rd_get_default(VS::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, VS::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
		ids_ptr[1] = storage->sampler_rd_get_default(VS::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, VS::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
		ids_ptr[2] = storage->sampler_rd_get_default(VS::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIMPAMPS, VS::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
		ids_ptr[3] = storage->sampler_rd_get_default(VS::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, VS::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
		ids_ptr[4] = storage->sampler_rd_get_default(VS::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIMPAMPS_ANISOTROPIC, VS::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
		ids_ptr[5] = storage->sampler_rd_get_default(VS::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC, VS::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
		ids_ptr[6] = storage->sampler_rd_get_default(VS::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, VS::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED);
		ids_ptr[7] = storage->sampler_rd_get_default(VS::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, VS::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED);
		ids_ptr[8] = storage->sampler_rd_get_default(VS::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIMPAMPS, VS::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED);
		ids_ptr[9] = storage->sampler_rd_get_default(VS::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, VS::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED);
		ids_ptr[10] = storage->sampler_rd_get_default(VS::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIMPAMPS_ANISOTROPIC, VS::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED);
		ids_ptr[11] = storage->sampler_rd_get_default(VS::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC, VS::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED);
		uniforms.push_back(u);
	}

	{
		RD::Uniform u;
		u.binding = 7;
		u.type = RD::UNIFORM_TYPE_SAMPLER;
		u.ids.push_back(shadow_sampler);
		uniforms.push_back(u);
	}

	{
		RD::Uniform u;
		u.binding = 8;
		u.type = RD::UNIFORM_TYPE_UNIFORM_BUFFER;
		u.ids.push_back(scene_state.uniform_buffer);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.binding = 9;
		u.type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
		u.ids.push_back(scene_state.instance_buffer);
		uniforms.push_back(u);
	}

	{
		RD::Uniform u;
		u.binding = 10;
		u.type = RD::UNIFORM_TYPE_UNIFORM_BUFFER;
		u.ids.push_back(scene_state.reflection_buffer);
		uniforms.push_back(u);
	}

	{

		RID ref_texture = p_reflection_atlas.is_valid() ? reflection_atlas_get_texture(p_reflection_atlas) : RID();
		RD::Uniform u;
		u.binding = 11;
		u.type = RD::UNIFORM_TYPE_TEXTURE;
		if (ref_texture.is_valid()) {
			u.ids.push_back(ref_texture);
		} else {
			u.ids.push_back(storage->texture_rd_get_default(RasterizerStorageRD::DEFAULT_RD_TEXTURE_CUBEMAP_ARRAY_BLACK));
		}
		uniforms.push_back(u);
	}

	{
		RD::Uniform u;
		u.binding = 12;
		u.type = RD::UNIFORM_TYPE_UNIFORM_BUFFER;
		u.ids.push_back(scene_state.light_buffer);
		uniforms.push_back(u);
	}

	{
		RD::Uniform u;
		u.binding = 13;
		u.type = RD::UNIFORM_TYPE_TEXTURE;
		if (p_shadow_atlas.is_valid()) {
			u.ids.push_back(shadow_atlas_get_texture(p_shadow_atlas));
		} else {
			u.ids.push_back(storage->texture_rd_get_default(RasterizerStorageRD::DEFAULT_RD_TEXTURE_WHITE));
		}
		uniforms.push_back(u);
	}

	{
		RD::Uniform u;
		u.binding = 14;
		u.type = RD::UNIFORM_TYPE_UNIFORM_BUFFER;
		u.ids.push_back(scene_state.directional_light_buffer);
		uniforms.push_back(u);
	}

	{
		RD::Uniform u;
		u.binding = 15;
		u.type = RD::UNIFORM_TYPE_TEXTURE;
		if (directional_shadow_get_texture().is_valid()) {
			u.ids.push_back(directional_shadow_get_texture());
		} else {
			u.ids.push_back(storage->texture_rd_get_default(RasterizerStorageRD::DEFAULT_RD_TEXTURE_WHITE));
		}
		uniforms.push_back(u);
	}

	render_base_uniform_set = RD::get_singleton()->uniform_set_create(uniforms, default_shader_rd, 0);
}

RasterizerSceneForwardRD *RasterizerSceneForwardRD::singleton = NULL;

void RasterizerSceneForwardRD::set_time(double p_time) {
	time = p_time;
}

RasterizerSceneForwardRD::RasterizerSceneForwardRD(RasterizerStorageRD *p_storage) :
		RasterizerSceneRD(p_storage) {
	singleton = this;
	storage = p_storage;

	/* SHADER */

	{
		String defines;
		defines += "\n#define MAX_ROUGHNESS_LOD " + itos(get_roughness_layers() - 1) + ".0\n";
		if (is_using_radiance_cubemap_array()) {
			defines += "\n#define USE_RADIANCE_CUBEMAP_ARRAY \n";
		}

		uint32_t uniform_max_size = RD::get_singleton()->limit_get(RD::LIMIT_MAX_UNIFORM_BUFFER_SIZE);

		{ //reflections
			uint32_t reflection_buffer_size;
			if (uniform_max_size < 65536) {
				//Yes, you guessed right, ARM again
				reflection_buffer_size = uniform_max_size;
			} else {
				reflection_buffer_size = 65536;
			}

			scene_state.max_reflections = reflection_buffer_size / sizeof(ReflectionData);
			scene_state.reflections = memnew_arr(ReflectionData, scene_state.max_reflections);
			scene_state.reflection_buffer = RD::get_singleton()->uniform_buffer_create(reflection_buffer_size);
			defines += "\n#define MAX_REFLECTION_DATA_STRUCTS " + itos(scene_state.max_reflections) + "\n";
		}

		{ //lights
			scene_state.max_lights = MIN(65536, uniform_max_size) / sizeof(LightData);
			uint32_t light_buffer_size = scene_state.max_lights * sizeof(LightData);
			print_line("ID: " + itos(sizeof(InstanceData)));
			scene_state.lights = memnew_arr(LightData, scene_state.max_lights);
			scene_state.light_buffer = RD::get_singleton()->uniform_buffer_create(light_buffer_size);
			defines += "\n#define MAX_LIGHT_DATA_STRUCTS " + itos(scene_state.max_lights) + "\n";

			scene_state.max_directional_lights = 8;
			uint32_t directional_light_buffer_size = scene_state.max_directional_lights * sizeof(DirectionalLightData);
			scene_state.directional_lights = memnew_arr(DirectionalLightData, scene_state.max_directional_lights);
			scene_state.directional_light_buffer = RD::get_singleton()->uniform_buffer_create(directional_light_buffer_size);
			defines += "\n#define MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS " + itos(scene_state.max_directional_lights) + "\n";
		}

		Vector<String> shader_versions;
		shader_versions.push_back("\n#define MODE_RENDER_DEPTH\n");
		shader_versions.push_back("\n#define MODE_RENDER_DEPTH\n#define MODE_DUAL_PARABOLOID\n");
		shader_versions.push_back("\n#define MODE_RENDER_DEPTH\n#define ENABLE_WRITE_NORMAL_BUFFER\n");
		shader_versions.push_back("\n#define MODE_RENDER_DEPTH\n#define ENABLE_WRITE_NORMAL_ROUGHNESS_BUFFER\n");
		shader_versions.push_back("");
		shader_versions.push_back("\n#define MODE_MULTIPLE_RENDER_TARGETS\n");
		shader_versions.push_back("\n#define USE_VOXEL_CONE_TRACING\n");
		shader_versions.push_back("\n#define MODE_MULTIPLE_RENDER_TARGETS\n#define USE_VOXEL_CONE_TRACING\n");
		shader_versions.push_back("\n#define USE_LIGHTMAP\n");
		shader_versions.push_back("\n#define MODE_MULTIPLE_RENDER_TARGETS\n#define USE_LIGHTMAP\n");
		shader.scene_shader.initialize(shader_versions, defines);
	}

	storage->shader_set_data_request_function(RasterizerStorageRD::SHADER_TYPE_3D, _create_shader_funcs);
	storage->material_set_data_request_function(RasterizerStorageRD::SHADER_TYPE_3D, _create_material_funcs);

	{
		//shader compiler
		ShaderCompilerRD::DefaultIdentifierActions actions;

		actions.renames["WORLD_MATRIX"] = "world_matrix";
		actions.renames["WORLD_NORMAL_MATRIX"] = "world_normal_matrix";
		actions.renames["INV_CAMERA_MATRIX"] = "scene_data.inv_camera_matrix";
		actions.renames["CAMERA_MATRIX"] = "scene_data.camera_matrix";
		actions.renames["PROJECTION_MATRIX"] = "projection_matrix";
		actions.renames["INV_PROJECTION_MATRIX"] = "scene_data.inv_projection_matrix";
		actions.renames["MODELVIEW_MATRIX"] = "modelview";
		actions.renames["MODELVIEW_NORMAL_MATRIX"] = "modelview_normal";

		actions.renames["VERTEX"] = "vertex";
		actions.renames["NORMAL"] = "normal";
		actions.renames["TANGENT"] = "tangent";
		actions.renames["BINORMAL"] = "binormal";
		actions.renames["POSITION"] = "position";
		actions.renames["UV"] = "uv_interp";
		actions.renames["UV2"] = "uv2_interp";
		actions.renames["COLOR"] = "color_interp";
		actions.renames["POINT_SIZE"] = "gl_PointSize";
		actions.renames["INSTANCE_ID"] = "gl_InstanceIndex";

		//builtins

		actions.renames["TIME"] = "scene_data.time";
		actions.renames["VIEWPORT_SIZE"] = "scene_data.viewport_size";

		actions.renames["FRAGCOORD"] = "gl_FragCoord";
		actions.renames["FRONT_FACING"] = "gl_FrontFacing";
		actions.renames["NORMALMAP"] = "normalmap";
		actions.renames["NORMALMAP_DEPTH"] = "normaldepth";
		actions.renames["ALBEDO"] = "albedo";
		actions.renames["ALPHA"] = "alpha";
		actions.renames["METALLIC"] = "metallic";
		actions.renames["SPECULAR"] = "specular";
		actions.renames["ROUGHNESS"] = "roughness";
		actions.renames["RIM"] = "rim";
		actions.renames["RIM_TINT"] = "rim_tint";
		actions.renames["CLEARCOAT"] = "clearcoat";
		actions.renames["CLEARCOAT_GLOSS"] = "clearcoat_gloss";
		actions.renames["ANISOTROPY"] = "anisotropy";
		actions.renames["ANISOTROPY_FLOW"] = "anisotropy_flow";
		actions.renames["SSS_STRENGTH"] = "sss_strength";
		actions.renames["TRANSMISSION"] = "transmission";
		actions.renames["AO"] = "ao";
		actions.renames["AO_LIGHT_AFFECT"] = "ao_light_affect";
		actions.renames["EMISSION"] = "emission";
		actions.renames["POINT_COORD"] = "gl_PointCoord";
		actions.renames["INSTANCE_CUSTOM"] = "instance_custom";
		actions.renames["SCREEN_UV"] = "screen_uv";
		actions.renames["SCREEN_TEXTURE"] = "screen_texture";
		actions.renames["DEPTH_TEXTURE"] = "depth_buffer";
		actions.renames["NORMAL_TEXTURE"] = "normal_buffer";
		actions.renames["DEPTH"] = "gl_FragDepth";
		actions.renames["OUTPUT_IS_SRGB"] = "true";

		//for light
		actions.renames["VIEW"] = "view";
		actions.renames["LIGHT_COLOR"] = "light_color";
		actions.renames["LIGHT"] = "light";
		actions.renames["ATTENUATION"] = "attenuation";
		actions.renames["DIFFUSE_LIGHT"] = "diffuse_light";
		actions.renames["SPECULAR_LIGHT"] = "specular_light";

		actions.usage_defines["TANGENT"] = "#define TANGENT_USED\n";
		actions.usage_defines["BINORMAL"] = "@TANGENT";
		actions.usage_defines["RIM"] = "#define LIGHT_RIM_USED\n";
		actions.usage_defines["RIM_TINT"] = "@RIM";
		actions.usage_defines["CLEARCOAT"] = "#define LIGHT_CLEARCOAT_USED\n";
		actions.usage_defines["CLEARCOAT_GLOSS"] = "@CLEARCOAT";
		actions.usage_defines["ANISOTROPY"] = "#define LIGHT_ANISOTROPY_USED\n";
		actions.usage_defines["ANISOTROPY_FLOW"] = "@ANISOTROPY";
		actions.usage_defines["AO"] = "#define AO_USED\n";
		actions.usage_defines["AO_LIGHT_AFFECT"] = "#define AO_USED\n";
		actions.usage_defines["UV"] = "#define UV_USED\n";
		actions.usage_defines["UV2"] = "#define UV2_USED\n";
		actions.usage_defines["NORMALMAP"] = "#define NORMALMAP_USED\n";
		actions.usage_defines["NORMALMAP_DEPTH"] = "@NORMALMAP";
		actions.usage_defines["COLOR"] = "#define COLOR_USED\n";
		actions.usage_defines["INSTANCE_CUSTOM"] = "#define ENABLE_INSTANCE_CUSTOM\n";
		actions.usage_defines["POSITION"] = "#define OVERRIDE_POSITION\n";

		actions.usage_defines["SSS_STRENGTH"] = "#define ENABLE_SSS\n";
		actions.usage_defines["TRANSMISSION"] = "#define LIGHT_TRANSMISSION_USED\n";
		actions.usage_defines["SCREEN_TEXTURE"] = "#define SCREEN_TEXTURE_USED\n";
		actions.usage_defines["SCREEN_UV"] = "#define SCREEN_UV_USED\n";

		actions.usage_defines["DIFFUSE_LIGHT"] = "#define USE_LIGHT_SHADER_CODE\n";
		actions.usage_defines["SPECULAR_LIGHT"] = "#define USE_LIGHT_SHADER_CODE\n";

		actions.render_mode_defines["skip_vertex_transform"] = "#define SKIP_TRANSFORM_USED\n";
		actions.render_mode_defines["world_vertex_coords"] = "#define VERTEX_WORLD_COORDS_USED\n";
		actions.render_mode_defines["ensure_correct_normals"] = "#define ENSURE_CORRECT_NORMALS\n";
		actions.render_mode_defines["cull_front"] = "#define DO_SIDE_CHECK\n";
		actions.render_mode_defines["cull_disabled"] = "#define DO_SIDE_CHECK\n";

		bool force_lambert = GLOBAL_GET("rendering/quality/shading/force_lambert_over_burley");

		if (!force_lambert) {
			actions.render_mode_defines["diffuse_burley"] = "#define DIFFUSE_BURLEY\n";
		}

		actions.render_mode_defines["diffuse_oren_nayar"] = "#define DIFFUSE_OREN_NAYAR\n";
		actions.render_mode_defines["diffuse_lambert_wrap"] = "#define DIFFUSE_LAMBERT_WRAP\n";
		actions.render_mode_defines["diffuse_toon"] = "#define DIFFUSE_TOON\n";

		bool force_blinn = GLOBAL_GET("rendering/quality/shading/force_blinn_over_ggx");

		if (!force_blinn) {
			actions.render_mode_defines["specular_schlick_ggx"] = "#define SPECULAR_SCHLICK_GGX\n";
		} else {
			actions.render_mode_defines["specular_schlick_ggx"] = "#define SPECULAR_BLINN\n";
		}

		actions.render_mode_defines["specular_blinn"] = "#define SPECULAR_BLINN\n";
		actions.render_mode_defines["specular_phong"] = "#define SPECULAR_PHONG\n";
		actions.render_mode_defines["specular_toon"] = "#define SPECULAR_TOON\n";
		actions.render_mode_defines["specular_disabled"] = "#define SPECULAR_DISABLED\n";
		actions.render_mode_defines["shadows_disabled"] = "#define SHADOWS_DISABLED\n";
		actions.render_mode_defines["ambient_light_disabled"] = "#define AMBIENT_LIGHT_DISABLED\n";
		actions.render_mode_defines["shadow_to_opacity"] = "#define USE_SHADOW_TO_OPACITY\n";

		actions.sampler_array_name = "material_samplers";
		actions.base_texture_binding_index = 1;
		actions.texture_layout_set = 2;
		actions.base_uniform_string = "material.";

		actions.default_filter = ShaderLanguage::FILTER_LINEAR_MIPMAP;
		actions.default_repeat = ShaderLanguage::REPEAT_ENABLE;

		shader.compiler.initialize(actions);
	}

	//render list
	render_list.max_elements = GLOBAL_DEF_RST("rendering/limits/rendering/max_renderable_elements", (int)128000);
	render_list.init();
	render_pass = 0;

	{

		scene_state.max_instances = render_list.max_elements;
		scene_state.instances = memnew_arr(InstanceData, scene_state.max_instances);
		scene_state.instance_buffer = RD::get_singleton()->storage_buffer_create(sizeof(InstanceData) * scene_state.max_instances);
	}

	scene_state.uniform_buffer = RD::get_singleton()->uniform_buffer_create(sizeof(SceneState::UBO));

	{
		//default material and shader
		default_shader = storage->shader_create();
		storage->shader_set_code(default_shader, "shader_type spatial; void vertex() { ROUGHNESS = 0.8; } void fragment() { ALBEDO=vec3(0.6); ROUGHNESS=0.8; METALLIC=0.2; } \n");
		default_material = storage->material_create();
		storage->material_set_shader(default_material, default_shader);

		MaterialData *md = (MaterialData *)storage->material_get_data(default_material, RasterizerStorageRD::SHADER_TYPE_3D);
		default_shader_rd = shader.scene_shader.version_get_shader(md->shader_data->version, SHADER_VERSION_COLOR_PASS);
	}

	{

		RD::SamplerState sampler;
		sampler.mag_filter = RD::SAMPLER_FILTER_LINEAR;
		sampler.min_filter = RD::SAMPLER_FILTER_LINEAR;
		sampler.enable_compare = true;
		sampler.compare_op = RD::COMPARE_OP_LESS;
		shadow_sampler = RD::get_singleton()->sampler_create(sampler);
	}
}

RasterizerSceneForwardRD::~RasterizerSceneForwardRD() {
	//clear base uniform set if still valid
	if (render_base_uniform_set.is_valid() && RD::get_singleton()->uniform_set_is_valid(render_base_uniform_set)) {
		RD::get_singleton()->free(render_base_uniform_set);
	}

	{
		RD::get_singleton()->free(scene_state.reflection_buffer);
		memdelete_arr(scene_state.reflections);
	}
}