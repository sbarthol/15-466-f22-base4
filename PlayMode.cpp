#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <random>

GLuint hexapod_meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > hexapod_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("hexapod.pnct"));
	hexapod_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > hexapod_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("hexapod.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = hexapod_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = hexapod_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;

	});
});

Load< Sound::Sample > dusty_floor_sample(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("dusty-floor.opus"));
});

// https://gedge.ca/blog/2013-12-08-opengl-text-rendering-with-freetype
// https://www.freetype.org/freetype2/docs/tutorial/step1.html
// https://github.com/harfbuzz/harfbuzz-tutorial/blob/master/hello-harfbuzz-freetype.c
void PlayMode::draw_text(std::string s, glm::uvec2 const &drawable_size) {

  const char *text = s.c_str();

	/* Create hb-buffer and populate. */
  hb_buffer_t *hb_buffer = hb_buffer_create ();
  hb_buffer_add_utf8 (hb_buffer, text, -1, 0, -1);
  hb_buffer_guess_segment_properties (hb_buffer);

  /* Shape it! */
  hb_shape (hb_font, hb_buffer, NULL, 0);

  /* Get glyph information and positions out of the buffer. */
  unsigned int len = hb_buffer_get_length (hb_buffer);
  hb_glyph_info_t *info = hb_buffer_get_glyph_infos (hb_buffer, NULL);
  hb_glyph_position_t *pos = hb_buffer_get_glyph_positions (hb_buffer, NULL);

  /* And converted to absolute positions. */
  {
    double current_x = 0;
    double current_y = 0;
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1); 

    for (unsigned int i = 0; i < len; i++)
    {
      hb_codepoint_t glyph_index   = info[i].codepoint;

			/* load glyph image into the slot (erase previous one) */
  		FT_Error error = FT_Load_Glyph( ft_face, glyph_index, FT_LOAD_DEFAULT );
  		if ( error )
    		continue;  /* ignore errors */

			/* convert to an anti-aliased bitmap */
  		error = FT_Render_Glyph( ft_face->glyph, FT_RENDER_MODE_NORMAL );
  		if ( error )
    		continue;


      float x_position = current_x + pos[i].x_offset / (64.f * drawable_size.x);
      float y_position = current_y + ((pos[i].y_offset / 64.f) - ((ft_face->bbox.yMax - ft_face->bbox.yMin) - ft_face->glyph->metrics.horiBearingY) / 64.f) / (drawable_size.y);

			FT_Bitmap *bm = &ft_face->glyph->bitmap;
			// FT_PIXEL_MODE_GRAY
			// each pixel stored in 8 bits

    	glTexImage2D(
      	GL_TEXTURE_2D,
        0,
        GL_R8,
        ft_face->glyph->bitmap.width,
        ft_face->glyph->bitmap.rows,
        0,
        GL_RED,
        GL_UNSIGNED_BYTE,
        ft_face->glyph->bitmap.buffer
    	);

      const float w = bm->width / (float)drawable_size.x;
      const float h = bm->rows/ (float)drawable_size.y;

      struct {
      	float x, y, s, t;
      } data[6] = {
      	{x_position    , y_position    , 0, 0},
        {x_position    , y_position - h, 0, 1},
        {x_position + w, y_position    , 1, 0},
        {x_position + w, y_position    , 1, 0},
        {x_position    , y_position - h, 0, 1},
        {x_position + w, y_position - h, 1, 1}
      };

      glBufferData(GL_ARRAY_BUFFER, 24*sizeof(float), data, GL_DYNAMIC_DRAW);
      glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0);
      glDrawArrays(GL_TRIANGLES, 0, 6);

      current_x += pos[i].x_advance / (64.f * drawable_size.x);
      current_y += pos[i].y_advance / (64.f * drawable_size.y);

			assert(current_x <= 1.0);
			assert(current_y <= 1.0);
    }
  }

	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	GL_ERRORS();

}

PlayMode::PlayMode() : scene(*hexapod_scene) {
	//get pointers to leg for convenience:
	for (auto &transform : scene.transforms) {
		if (transform.name == "Hip.FL") hip = &transform;
		else if (transform.name == "UpperLeg.FL") upper_leg = &transform;
		else if (transform.name == "LowerLeg.FL") lower_leg = &transform;
	}
	if (hip == nullptr) throw std::runtime_error("Hip not found.");
	if (upper_leg == nullptr) throw std::runtime_error("Upper leg not found.");
	if (lower_leg == nullptr) throw std::runtime_error("Lower leg not found.");

	hip_base_rotation = hip->rotation;
	upper_leg_base_rotation = upper_leg->rotation;
	lower_leg_base_rotation = lower_leg->rotation;

	//get pointer to camera for convenience:
	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));
	camera = &scene.cameras.front();

	//start music loop playing:
	// (note: position will be over-ridden in update())
	leg_tip_loop = Sound::loop_3D(*dusty_floor_sample, 1.0f, get_leg_tip_position(), 10.0f);


	// Initialize our texture and VBOs
  glGenBuffers(1, &vbo);
  glGenVertexArrays(1, &vao);
  glGenTextures(1, &texture);
  glGenSamplers(1, &sampler);
  glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// Initialize shader
  vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &VERTEX_SHADER, 0);
  glCompileShader(vs);

  fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, &FRAGMENT_SHADER, 0);
  glCompileShader(fs);

  program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);

  // Set some initialize GL state
  glEnable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glClearColor(0.1, 0.2, 0.4, 0);

  // Get shader uniforms
  glUseProgram(program);
  texUniform = glGetUniformLocation(program, "tex");

	#define FONT_SIZE 72

	const char *fontfile = "/Users/sacha/Desktop/15666/harfbuzz-tutorial/Roboto-Regular.ttf";

	/* Initialize FreeType and create FreeType font face. */
  FT_Error ft_error;

  if ((ft_error = FT_Init_FreeType (&ft_library)))
    abort();
  if ((ft_error = FT_New_Face (ft_library, fontfile, 0, &ft_face)))
    abort();
  if ((ft_error = FT_Set_Char_Size (ft_face, FONT_SIZE*64, FONT_SIZE*64, 0, 0)))
    abort();

  /* Create hb-ft font. */
  hb_font = hb_ft_font_create (ft_face, NULL);
}

PlayMode::~PlayMode() {
	FT_Done_Face(ft_face);
	FT_Done_FreeType(ft_library);
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_KEYDOWN) {
		if (evt.key.keysym.sym == SDLK_ESCAPE) {
			SDL_SetRelativeMouseMode(SDL_FALSE);
			return true;
		} else if (evt.key.keysym.sym == SDLK_a) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.downs += 1;
			down.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_a) {
			left.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.pressed = false;
			return true;
		}
	} else if (evt.type == SDL_MOUSEBUTTONDOWN) {
		if (SDL_GetRelativeMouseMode() == SDL_FALSE) {
			SDL_SetRelativeMouseMode(SDL_TRUE);
			return true;
		}
	} else if (evt.type == SDL_MOUSEMOTION) {
		if (SDL_GetRelativeMouseMode() == SDL_TRUE) {
			glm::vec2 motion = glm::vec2(
				evt.motion.xrel / float(window_size.y),
				-evt.motion.yrel / float(window_size.y)
			);
			camera->transform->rotation = glm::normalize(
				camera->transform->rotation
				* glm::angleAxis(-motion.x * camera->fovy, glm::vec3(0.0f, 1.0f, 0.0f))
				* glm::angleAxis(motion.y * camera->fovy, glm::vec3(1.0f, 0.0f, 0.0f))
			);
			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {

	//slowly rotates through [0,1):
	wobble += elapsed / 10.0f;
	wobble -= std::floor(wobble);

	hip->rotation = hip_base_rotation * glm::angleAxis(
		glm::radians(5.0f * std::sin(wobble * 2.0f * float(M_PI))),
		glm::vec3(0.0f, 1.0f, 0.0f)
	);
	upper_leg->rotation = upper_leg_base_rotation * glm::angleAxis(
		glm::radians(7.0f * std::sin(wobble * 2.0f * 2.0f * float(M_PI))),
		glm::vec3(0.0f, 0.0f, 1.0f)
	);
	lower_leg->rotation = lower_leg_base_rotation * glm::angleAxis(
		glm::radians(10.0f * std::sin(wobble * 3.0f * 2.0f * float(M_PI))),
		glm::vec3(0.0f, 0.0f, 1.0f)
	);

	//move sound to follow leg tip position:
	leg_tip_loop->set_position(get_leg_tip_position(), 1.0f / 60.0f);

	//move camera:
	{

		//combine inputs into a move:
		constexpr float PlayerSpeed = 30.0f;
		glm::vec2 move = glm::vec2(0.0f);
		if (left.pressed && !right.pressed) move.x =-1.0f;
		if (!left.pressed && right.pressed) move.x = 1.0f;
		if (down.pressed && !up.pressed) move.y =-1.0f;
		if (!down.pressed && up.pressed) move.y = 1.0f;

		//make it so that moving diagonally doesn't go faster:
		if (move != glm::vec2(0.0f)) move = glm::normalize(move) * PlayerSpeed * elapsed;

		glm::mat4x3 frame = camera->transform->make_local_to_parent();
		glm::vec3 frame_right = frame[0];
		//glm::vec3 up = frame[1];
		glm::vec3 frame_forward = -frame[2];

		camera->transform->position += move.x * frame_right + move.y * frame_forward;
	}

	{ //update listener to camera position:
		glm::mat4x3 frame = camera->transform->make_local_to_parent();
		glm::vec3 frame_right = frame[0];
		glm::vec3 frame_at = frame[3];
		Sound::listener.set_position_right(frame_at, frame_right, 1.0f / 60.0f);
	}

	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//update camera aspect ratio for drawable:
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	//set up light type and position for lit_color_texture_program:
	// TODO: consider using the Light(s) in the scene to do this
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	scene.draw(*camera);

	{ //use DrawLines to overlay some text:
		glDisable(GL_DEPTH_TEST);
		float aspect = float(drawable_size.x) / float(drawable_size.y);
		DrawLines lines(glm::mat4(
			1.0f / aspect, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		));

		constexpr float H = 0.09f;
		lines.draw_text("Mouse motion rotates camera; WASD moves; escape ungrabs mouse",
			glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0x00, 0x00, 0x00, 0x00));
		float ofs = 2.0f / drawable_size.y;
		lines.draw_text("Mouse motion rotates camera; WASD moves; escape ungrabs mouse",
			glm::vec3(-aspect + 0.1f * H + ofs, -1.0 + + 0.1f * H + ofs, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0xff, 0xff, 0xff, 0x00));
	}

	// Bind stuff
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glBindSampler(0, sampler);
  glBindVertexArray(vao);
  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glUseProgram(program);
  glUniform1i(texUniform, 0);
	draw_text("Hello World, my name is Sacha! I am the guy. abcdABCD", drawable_size);

	GL_ERRORS();
}

glm::vec3 PlayMode::get_leg_tip_position() {
	//the vertex position here was read from the model in blender:
	return lower_leg->make_local_to_world() * glm::vec4(-1.26137f, -11.861f, 0.0f, 1.0f);
}
