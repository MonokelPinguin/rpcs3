#include "stdafx.h"
#include "Emu/System.h"

#include "VKVertexProgram.h"
#include "VKCommonDecompiler.h"
#include "VKHelpers.h"

std::string VKVertexDecompilerThread::getFloatTypeName(size_t elementCount)
{
	return vk::getFloatTypeNameImpl(elementCount);
}

std::string VKVertexDecompilerThread::getIntTypeName(size_t elementCount)
{
	return "ivec4";
}


std::string VKVertexDecompilerThread::getFunction(FUNCTION f)
{
	return vk::getFunctionImpl(f);
}

std::string VKVertexDecompilerThread::compareFunction(COMPARE f, const std::string &Op0, const std::string &Op1)
{
	return vk::compareFunctionImpl(f, Op0, Op1);
}

void VKVertexDecompilerThread::insertHeader(std::stringstream &OS)
{
	OS << "#version 450" << std::endl << std::endl;
	OS << "#extension GL_ARB_separate_shader_objects : enable" << std::endl;
	OS << "layout(std140, set = 0, binding = 0) uniform ScaleOffsetBuffer" << std::endl;
	OS << "{" << std::endl;
	OS << "	mat4 scaleOffsetMat;" << std::endl;
	OS << "	ivec4 userClipEnabled[2];" << std::endl;
	OS << "	vec4 userClipFactor[2];" << std::endl;
	OS << "};" << std::endl;

	vk::glsl::program_input in;
	in.location = 0;
	in.domain = vk::glsl::glsl_vertex_program;
	in.name = "ScaleOffsetBuffer";
	in.type = vk::glsl::input_type_uniform_buffer;

	inputs.push_back(in);
}

void VKVertexDecompilerThread::insertInputs(std::stringstream & OS, const std::vector<ParamType>& inputs)
{
	std::vector<std::tuple<size_t, std::string>> input_data;
	for (const ParamType &PT : inputs)
	{
		for (const ParamItem &PI : PT.items)
		{
			input_data.push_back(std::make_tuple(PI.location, PI.name));
		}
	}

	/**
	 * Its is important that the locations are in the order that vertex attributes are expected.
	 * If order is not adhered to, channels may be swapped leading to corruption
	*/

	std::sort(input_data.begin(), input_data.end());

	int location = 2;
	for (const std::tuple<size_t, std::string> item : input_data)
	{
		for (const ParamType &PT : inputs)
		{
			for (const ParamItem &PI : PT.items)
			{
				if (PI.name == std::get<1>(item))
				{
					vk::glsl::program_input in;
					in.location = location;
					in.domain = vk::glsl::glsl_vertex_program;
					in.name = PI.name + "_buffer";
					in.type = vk::glsl::input_type_texel_buffer;

					this->inputs.push_back(in);
					
					bool is_int = false;
					for (auto &attrib : rsx_vertex_program.rsx_vertex_inputs)
					{
						if (attrib.location == std::get<0>(item))
						{
							if (attrib.int_type) is_int = true;
							break;
						}
					}

					std::string samplerType = is_int ? "isamplerBuffer" : "samplerBuffer";
					OS << "layout(set = 0, binding=" << 3 + location++ << ")" << "	uniform " << samplerType << " " << PI.name << "_buffer;" << std::endl;
				}
			}
		}
	}
}

void VKVertexDecompilerThread::insertConstants(std::stringstream & OS, const std::vector<ParamType> & constants)
{
	OS << "layout(std140, set=0, binding = 1) uniform VertexConstantsBuffer" << std::endl;
	OS << "{" << std::endl;
	OS << "	vec4 vc[468];" << std::endl;
	OS << "	uint transform_branch_bits;" << std::endl;
	OS << "};" << std::endl << std::endl;

	vk::glsl::program_input in;
	in.location = 1;
	in.domain = vk::glsl::glsl_vertex_program;
	in.name = "VertexConstantsBuffer";
	in.type = vk::glsl::input_type_uniform_buffer;

	inputs.push_back(in);

	//We offset this value by the index of the first fragment texture (19) below
	//and allow 16 fragment textures to precede this slot
	int location = 16;

	for (const ParamType &PT : constants)
	{
		for (const ParamItem &PI : PT.items)
		{
			if (PI.name == "vc[468]")
				continue;

			if (PT.type == "sampler2D" ||
				PT.type == "samplerCube" ||
				PT.type == "sampler1D" ||
				PT.type == "sampler3D")
			{
				in.location = location;
				in.name = PI.name;
				in.type = vk::glsl::input_type_texture;

				inputs.push_back(in);

				OS << "layout(set = 0, binding=" << 19 + location++ << ") uniform " << PT.type << " " << PI.name << ";" << std::endl;
			}
		}
	}
}

static const vertex_reg_info reg_table[] =
{
	{ "gl_Position", false, "dst_reg0", "", false },
	{ "back_diff_color", true, "dst_reg1", "", false },
	{ "back_spec_color", true, "dst_reg2", "", false },
	{ "front_diff_color", true, "dst_reg3", "", false },
	{ "front_spec_color", true, "dst_reg4", "", false },
	{ "fog_c", true, "dst_reg5", ".xxxx", true, "", "", "", true, CELL_GCM_ATTRIB_OUTPUT_MASK_FOG },
	//Warning: With spir-v if you declare clip distance var, you must assign a value even when its disabled! Runtime does not assign a default value
	{ "gl_ClipDistance[0]", false, "dst_reg5", ".y * userClipFactor[0].x", false, "userClipEnabled[0].x > 0", "0.5", "", true, CELL_GCM_ATTRIB_OUTPUT_MASK_UC0 },
	{ "gl_ClipDistance[1]", false, "dst_reg5", ".z * userClipFactor[0].y", false, "userClipEnabled[0].y > 0", "0.5", "", true, CELL_GCM_ATTRIB_OUTPUT_MASK_UC1 },
	{ "gl_ClipDistance[2]", false, "dst_reg5", ".w * userClipFactor[0].z", false, "userClipEnabled[0].z > 0", "0.5", "", true, CELL_GCM_ATTRIB_OUTPUT_MASK_UC2 },
	{ "gl_PointSize", false, "dst_reg6", ".x", false },
	{ "gl_ClipDistance[3]", false, "dst_reg6", ".y * userClipFactor[0].w", false, "userClipEnabled[0].w > 0", "0.5", "", true, CELL_GCM_ATTRIB_OUTPUT_MASK_UC3 },
	{ "gl_ClipDistance[4]", false, "dst_reg6", ".z * userClipFactor[1].x", false, "userClipEnabled[1].x > 0", "0.5", "", true, CELL_GCM_ATTRIB_OUTPUT_MASK_UC4 },
	{ "gl_ClipDistance[5]", false, "dst_reg6", ".w * userClipFactor[1].y", false, "userClipEnabled[1].y > 0", "0.5", "", true, CELL_GCM_ATTRIB_OUTPUT_MASK_UC5 },
	{ "tc0", true, "dst_reg7", "", false },
	{ "tc1", true, "dst_reg8", "", false },
	{ "tc2", true, "dst_reg9", "", false },
	{ "tc3", true, "dst_reg10", "", false },
	{ "tc4", true, "dst_reg11", "", false },
	{ "tc5", true, "dst_reg12", "", false },
	{ "tc6", true, "dst_reg13", "", false },
	{ "tc7", true, "dst_reg14", "", false },
	{ "tc8", true, "dst_reg15", "", false },
	{ "tc9", true, "dst_reg6", "", false, "", "", "", true, CELL_GCM_ATTRIB_OUTPUT_MASK_TEX9 }  // In this line, dst_reg6 is correct since dst_reg goes from 0 to 15.
};

void VKVertexDecompilerThread::insertOutputs(std::stringstream & OS, const std::vector<ParamType> & outputs)
{
	bool insert_front_diffuse = (rsx_vertex_program.output_mask & CELL_GCM_ATTRIB_OUTPUT_MASK_FRONTDIFFUSE) != 0;
	bool insert_back_diffuse = (rsx_vertex_program.output_mask & CELL_GCM_ATTRIB_OUTPUT_MASK_BACKDIFFUSE) != 0;

	bool insert_front_specular = (rsx_vertex_program.output_mask & CELL_GCM_ATTRIB_OUTPUT_MASK_FRONTSPECULAR) != 0;
	bool insert_back_specular = (rsx_vertex_program.output_mask & CELL_GCM_ATTRIB_OUTPUT_MASK_BACKSPECULAR) != 0;

	for (auto &i : reg_table)
	{
		if (m_parr.HasParam(PF_PARAM_NONE, "vec4", i.src_reg) && i.need_declare)
		{
			if (i.check_mask && (rsx_vertex_program.output_mask & i.check_mask_value) == 0)
				continue;

			if (i.name == "front_diff_color")
				insert_front_diffuse = false;

			if (i.name == "front_spec_color")
				insert_front_specular = false;

			const vk::varying_register_t &reg = vk::get_varying_register(i.name);
			OS << "layout(location=" << reg.reg_location << ") out vec4 " << i.name << ";" << std::endl;
		}
	}

	if (insert_back_diffuse && insert_front_diffuse)
		OS << "layout(location=" << vk::get_varying_register("front_diff_color").reg_location << ") out vec4 front_diff_color;" << std::endl;

	if (insert_back_specular && insert_front_specular)
		OS << "layout(location=" << vk::get_varying_register("front_spec_color").reg_location << ") out vec4 front_spec_color;" << std::endl;
}

namespace vk
{
	void add_input(std::stringstream & OS, const ParamItem &PI, const std::vector<rsx_vertex_input> &inputs)
	{
		for (const auto &real_input : inputs)
		{
			if (real_input.location != PI.location)
				continue;

			if (!real_input.is_array)
			{
				OS << "	vec4 " << PI.name << " = vec4(texelFetch(" << PI.name << "_buffer, 0));" << std::endl;
				return;
			}

			if (real_input.frequency > 1)
			{
				if (real_input.is_modulo)
				{
					OS << "	vec4 " << PI.name << "= vec4(texelFetch(" << PI.name << "_buffer, gl_VertexIndex %" << real_input.frequency << "));" << std::endl;
					return;
				}

				OS << "	vec4 " << PI.name << "= vec4(texelFetch(" << PI.name << "_buffer, gl_VertexIndex /" << real_input.frequency << "));" << std::endl;
				return;
			}

			OS << "	vec4 " << PI.name << "= vec4(texelFetch(" << PI.name << "_buffer, gl_VertexIndex).rgba);" << std::endl;
			return;
		}

		OS << "	vec4 " << PI.name << "= vec4(texelFetch(" << PI.name << "_buffer, gl_VertexIndex).rgba);" << std::endl;
	}
}

void VKVertexDecompilerThread::insertMainStart(std::stringstream & OS)
{
	vk::insert_glsl_legacy_function(OS, vk::glsl::program_domain::glsl_vertex_program);

	std::string parameters = "";
	for (int i = 0; i < 16; ++i)
	{
		std::string reg_name = "dst_reg" + std::to_string(i);
		if (m_parr.HasParam(PF_PARAM_NONE, "vec4", reg_name))
		{
			if (parameters.length())
				parameters += ", ";

			parameters += "inout vec4 " + reg_name;
		}
	}

	OS << "void vs_main(" << parameters << ")" << std::endl;
	OS << "{" << std::endl;

	//Declare temporary registers, ignoring those mapped to outputs
	for (const ParamType PT : m_parr.params[PF_PARAM_NONE])
	{
		for (const ParamItem &PI : PT.items)
		{
			if (PI.name.substr(0, 7) == "dst_reg")
				continue;

			OS << "	" << PT.type << " " << PI.name;
			if (!PI.value.empty())
				OS << " = " << PI.value;

			OS << ";" << std::endl;
		}
	}

	for (const ParamType &PT : m_parr.params[PF_PARAM_IN])
	{
		for (const ParamItem &PI : PT.items)
			vk::add_input(OS, PI, rsx_vertex_program.rsx_vertex_inputs);
	}
}

void VKVertexDecompilerThread::insertMainEnd(std::stringstream & OS)
{
	OS << "}" << std::endl << std::endl;

	OS << "void main ()" << std::endl;
	OS << "{" << std::endl;

	std::string parameters = "";

	if (ParamType *vec4Types = m_parr.SearchParam(PF_PARAM_NONE, "vec4"))
	{
		for (int i = 0; i < 16; ++i)
		{
			std::string reg_name = "dst_reg" + std::to_string(i);
			for (auto &PI : vec4Types->items)
			{
				if (reg_name == PI.name)
				{
					if (parameters.length())
						parameters += ", ";

					parameters += reg_name;
					OS << "	vec4 " << reg_name;

					if (!PI.value.empty())
						OS << "= " << PI.value;

					OS << ";" << std::endl;
				}
			}
		}
	}

	OS << std::endl << "	vs_main(" << parameters << ");" << std::endl << std::endl;

	bool insert_front_diffuse = (rsx_vertex_program.output_mask & CELL_GCM_ATTRIB_OUTPUT_MASK_FRONTDIFFUSE) != 0;
	bool insert_front_specular = (rsx_vertex_program.output_mask & CELL_GCM_ATTRIB_OUTPUT_MASK_FRONTSPECULAR) != 0;

	bool insert_back_diffuse = (rsx_vertex_program.output_mask & CELL_GCM_ATTRIB_OUTPUT_MASK_BACKDIFFUSE) != 0;
	bool insert_back_specular = (rsx_vertex_program.output_mask & CELL_GCM_ATTRIB_OUTPUT_MASK_BACKSPECULAR) != 0;

	for (auto &i : reg_table)
	{
		if (m_parr.HasParam(PF_PARAM_NONE, "vec4", i.src_reg))
		{
			if (i.check_mask && (rsx_vertex_program.output_mask & i.check_mask_value) == 0)
				continue;

			if (i.name == "front_diff_color")
				insert_front_diffuse = false;

			if (i.name == "front_spec_color")
				insert_front_specular = false;

			std::string condition = (!i.cond.empty()) ? "(" + i.cond + ") " : "";

			if (condition.empty() || i.default_val.empty())
			{
				if (!condition.empty()) condition = "if " + condition;
				OS << "	" << condition << i.name << " = " << i.src_reg << i.src_reg_mask << ";" << std::endl;
			}
			else
			{
				//Insert if-else condition
				OS << "	" << i.name << " = " << condition << "? " << i.src_reg << i.src_reg_mask << ": " << i.default_val << ";" << std::endl;
			}
		}
		else if (i.need_declare && (rsx_vertex_program.output_mask & i.check_mask_value) > 0)
		{
			//An output was declared but nothing was written to it
			//Set it to all ones (Atelier Escha)
			OS << "	" << i.name << " = vec4(1.);" << std::endl;
		}
	}

	if (insert_back_diffuse && insert_front_diffuse)
		if (m_parr.HasParam(PF_PARAM_NONE, "vec4", "dst_reg1"))
			OS << "	front_diff_color = dst_reg1;\n";

	if (insert_back_specular && insert_front_specular)
		if (m_parr.HasParam(PF_PARAM_NONE, "vec4", "dst_reg2"))
			OS << "	front_spec_color = dst_reg2;\n";

	OS << "	gl_Position = gl_Position * scaleOffsetMat;" << std::endl;
	OS << "}" << std::endl;
}


void VKVertexDecompilerThread::Task()
{
	m_shader = Decompile();
	vk_prog->SetInputs(inputs);
}

VKVertexProgram::VKVertexProgram()
{
}

VKVertexProgram::~VKVertexProgram()
{
	Delete();
}

void VKVertexProgram::Decompile(const RSXVertexProgram& prog)
{
	VKVertexDecompilerThread decompiler(prog, shader, parr, *this);
	decompiler.Task();
}

void VKVertexProgram::Compile()
{
	fs::create_path(fs::get_config_dir() + "/shaderlog");
	fs::file(fs::get_config_dir() + "shaderlog/VertexProgram.spirv", fs::rewrite).write(shader);

	std::vector<u32> spir_v;
	if (!vk::compile_glsl_to_spv(shader, vk::glsl::glsl_vertex_program, spir_v))
		fmt::throw_exception("Failed to compile vertex shader" HERE);

	VkShaderModuleCreateInfo vs_info;
	vs_info.codeSize = spir_v.size() * sizeof(u32);
	vs_info.pNext = nullptr;
	vs_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	vs_info.pCode = (uint32_t*)spir_v.data();
	vs_info.flags = 0;

	VkDevice dev = (VkDevice)*vk::get_current_renderer();
	vkCreateShaderModule(dev, &vs_info, nullptr, &handle);

	id = (u32)((u64)handle);
}

void VKVertexProgram::Delete()
{
	shader.clear();

	if (handle)
	{
		VkDevice dev = (VkDevice)*vk::get_current_renderer();
		vkDestroyShaderModule(dev, handle, nullptr);

		handle = nullptr;
	}
}

void VKVertexProgram::SetInputs(std::vector<vk::glsl::program_input>& inputs)
{
	for (auto &it : inputs)
	{
		uniforms.push_back(it);
	}
}
