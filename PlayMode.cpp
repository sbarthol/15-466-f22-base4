#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <fstream>

#include <random>

void PlayMode::load_lines_from_file(std::string filename) {
	lines.clear();
  std::ifstream input(filename);
  for (std::string line; getline(input, line);) {
		lines.push_back(line);
  }
}

void PlayMode::draw_text_lines(glm::uvec2 const &drawable_size, float x, float y) {
	size_t counter = 0;
	for( size_t i=0;i<lines.size();i++) {
		std::string line = lines[i];
		if (counter + line.size() <= letter_counter) {
			counter += line.size();
			draw_text_line(line,drawable_size,x,y);
		} else if (counter < letter_counter) {
			draw_text_line(line.substr(0,letter_counter - counter),drawable_size,x,y);
			counter = letter_counter;
		}
		
		y -= ft_face->size->metrics.height / (64.f * (float)drawable_size.y);
	}
}

// https://gedge.ca/blog/2013-12-08-opengl-text-rendering-with-freetype
// https://www.freetype.org/freetype2/docs/tutorial/step1.html
// https://github.com/harfbuzz/harfbuzz-tutorial/blob/master/hello-harfbuzz-freetype.c
void PlayMode::draw_text_line(std::string s, glm::uvec2 const &drawable_size, float current_x, float current_y) {

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


      float x_position = current_x + (pos[i].x_offset + 400 * 64.f) / (64.f * drawable_size.x);
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

      current_x += (pos[i].x_advance) / (64.f * drawable_size.x);
      current_y += pos[i].y_advance / (64.f * drawable_size.y);

			assert(current_x <= 1.0);
			assert(current_y <= 1.0);
    }
  }

	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	GL_ERRORS();

}

PlayMode::PlayMode() {

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
  glClearColor(0, 0, 0, 1);

  // Get shader uniforms
  glUseProgram(program);
  texUniform = glGetUniformLocation(program, "tex");

	#define FONT_SIZE 120

	// https://www.1001fonts.com/risque-font.html
	std::string fontfile = data_path("../scenes/Risque-Regular.ttf");

	/* Initialize FreeType and create FreeType font face. */
  FT_Error ft_error;

  if ((ft_error = FT_Init_FreeType (&ft_library)))
    abort();
  if ((ft_error = FT_New_Face (ft_library, fontfile.c_str(), 0, &ft_face)))
    abort();
  if ((ft_error = FT_Set_Char_Size (ft_face, FONT_SIZE*64, FONT_SIZE*64, 0, 0)))
    abort();

  /* Create hb-ft font. */
  hb_font = hb_ft_font_create (ft_face, NULL);

	current_level = 0;
	current_elapsed = 0.f;
	intermezzo = false;
	load_lines_from_file(data_path(levels[current_level]));
}

PlayMode::~PlayMode() {
	FT_Done_Face(ft_face);
	FT_Done_FreeType(ft_library);
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_KEYDOWN) {
		if (evt.key.keysym.sym == SDLK_a) {
			a.downs += 1;
			a.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_b) {
			b.downs += 1;
			b.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_c) {
			c.downs += 1;
			c.pressed = true;
			return true;
		} 
	}

	return false;
}

void PlayMode::update(float elapsed) {

	current_elapsed += elapsed;
	if(current_elapsed >= max_elapsed) {
		letter_counter++;
		current_elapsed = 0.0;
	}

	if(a.downs || b.downs || c.downs) {
		if(current_level > 0 && current_level + 1 < levels.size() && !intermezzo) {
			current_elapsed = 0.f;
			letter_counter = 0;
			std::string suffix = a.downs ? "_a" : b.downs ? "_b" : "_c";
			load_lines_from_file(data_path(levels[current_level] + suffix));
			intermezzo = true;
		} else if(current_level + 1 < levels.size()) {
			current_level++;
			letter_counter = 0;
			current_elapsed = 0.0;
			load_lines_from_file(data_path(levels[current_level]));
			intermezzo = false;
		}
	}

	//reset button press counters:
	a.downs = 0;
	b.downs = 0;
	c.downs = 0;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	
	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	// Bind stuff
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glBindSampler(0, sampler);
  glBindVertexArray(vao);
  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glUseProgram(program);
  glUniform1i(texUniform, 0);

	draw_text_lines(drawable_size,-0.8,0.8);

	GL_ERRORS();
}
