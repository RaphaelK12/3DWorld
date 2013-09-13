// 3D World - GL EXT/ARB extension interface code
// by Frank Gennari
// 4/25/02
#include "3DWorld.h"
#include "gl_ext_arb.h"
#include "function_registry.h"
#include "inlines.h" // for render_to_texture_t::render() stuff


void init_glew() {

// MacOSX check here, placeholder for eventual cross-platform porting
#if ((defined(__MACH__))&&(defined(__APPLE__)))
	// nothing
#else
	GLenum const err(glewInit());

	if (GLEW_OK != err) {
		std::cerr << "Error: " << glewGetErrorString(err) << endl;
		assert(0);
	}
#endif
	if (!glewIsSupported("GL_VERSION_3_3")) {
		std::cerr << "Error: GL version 3.3 not found" << endl;
		assert(0);
    }
	glEnable(GL_MULTISAMPLE); // only works when using a multisampling graphics context
}


// ***************** MULTITEXTURING *****************

unsigned const MAX_MULTITEX = 32; // max is GL_TEXTURE31


void set_active_texture(unsigned tu_id) {

	assert(tu_id < MAX_MULTITEX); // Note: Assumes textures are defined sequentially
	glActiveTexture((GL_TEXTURE0 + tu_id));
}


void select_multitex(int id, unsigned tu_id, bool enable, bool reset) {

	set_active_texture(tu_id);
	select_texture(id, enable);
	if (reset) {set_active_texture(0);}
}


void disable_multitex(unsigned tu_id, bool do_disable_texgen) {

	set_active_texture(tu_id);
	if (do_disable_texgen) {disable_texgen();}
	glDisable(GL_TEXTURE_2D);
	set_active_texture(0); // end back at texture 0
}


// ***************** 3D TEXTURES *****************


void bind_3d_texture(unsigned tid) {

	glBindTexture(GL_TEXTURE_3D, tid);
	assert(glIsTexture(tid));
}


unsigned create_3d_texture(unsigned xsz, unsigned ysz, unsigned zsz, unsigned ncomp, vector<unsigned char> const &data, int filter, int wrap) {

	assert(data.size() == ncomp*xsz*ysz*zsz);
	unsigned tid(0);
	glGenTextures(1, &tid);
	bind_3d_texture(tid);
	glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, filter);
	glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, wrap);
	glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, wrap);
	glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, wrap);
	glTexImage3D(GL_TEXTURE_3D, 0, get_internal_texture_format(ncomp), xsz, ysz, zsz, 0, get_texture_format(ncomp), GL_UNSIGNED_BYTE, &data.front());
	return tid;
}


void update_3d_texture(unsigned tid, unsigned xoff, unsigned yoff, unsigned zoff, unsigned xsz, unsigned ysz, unsigned zsz,
					   unsigned ncomp, unsigned char const *const data)
{
	assert(glIsTexture(tid));
	bind_3d_texture(tid);
	glTexSubImage3D(GL_TEXTURE_3D, 0, xoff, yoff, zoff, xsz, ysz, zsz, get_texture_format(ncomp), GL_UNSIGNED_BYTE, data);
}


void set_3d_texture_as_current(unsigned tid, unsigned tu_id) {

	assert(glIsTexture(tid));
	set_active_texture(tu_id);
	bind_3d_texture(tid);
	set_active_texture(0);
}


// ***************** FOG_COORD *****************


void setup_fog_coord() {

	if (!glFogCoordf) {
		cout << "*** Can't find fog_coord extension ***" << endl;
		assert(0);
	}
}

void set_fog_coord(GLfloat val) {
	glFogCoordf(val);
}


// ***************** VBOs / VAOs *****************


unsigned create_vbo() {
	unsigned vbo;
	assert(glGenBuffers);
	glGenBuffers(1, &vbo);
	assert(vbo > 0);
	return vbo;
}

int get_buffer_target(bool is_index) {return (is_index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER);}

void bind_vbo(unsigned vbo, bool is_index) { // okay if vbo is zero
	glBindBuffer(get_buffer_target(is_index), vbo);
}

void delete_vbo(unsigned vbo) {
	if (vbo == 0) return;
	glDeleteBuffers(1, &vbo);
}

void upload_vbo_data(void const *const data, size_t size, bool is_index) {
	// hard coded for drawing of static data
	glBufferData(get_buffer_target(is_index), size, data, GL_STATIC_DRAW);
}

void upload_vbo_sub_data(void const *const data, int offset, size_t size, bool is_index) {
	glBufferSubData(get_buffer_target(is_index), offset, size, data);
}


unsigned create_vao() {
	unsigned vao;
	assert(glGenVertexArrays);
	glGenVertexArrays(1, &vao); 
	assert(vao > 0);
	return vao;
}

void bind_vao(unsigned vao) { // okay if vao is zero
	glBindVertexArray(vao);
}

void delete_vao(unsigned vao) {
	if (vao == 0) return;
	glDeleteVertexArrays(1, &vao);
}


// ***************** FBOs *****************


void create_fbo(unsigned &fbo_id, unsigned tid, bool is_depth_fbo) {
	
	// Create a framebuffer object
	glGenFramebuffers(1, &fbo_id);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_id);
	
	if (is_depth_fbo) {
		// Instruct openGL that we won't bind a color texture with the currently binded FBO
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
	}
	
	// Attach the texture to FBO depth or color attachment point
	glFramebufferTexture2D(GL_FRAMEBUFFER, (is_depth_fbo ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0), GL_TEXTURE_2D, tid, 0);
	
	// Check FBO status
	GLenum const status(glCheckFramebufferStatus(GL_FRAMEBUFFER));
	assert(status == GL_FRAMEBUFFER_COMPLETE);
	
	// Switch back to window-system-provided framebuffer
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


void enable_fbo(unsigned &fbo_id, unsigned tid, bool is_depth_fbo) {

	assert(glIsTexture(tid));
	if (!fbo_id) create_fbo(fbo_id, tid, is_depth_fbo);
	assert(fbo_id > 0);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_id); // Rendering offscreen
}


void disable_fbo() {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


void free_fbo(unsigned &fbo_id) {

	if (fbo_id > 0) {glDeleteFramebuffers(1, &fbo_id);}
	fbo_id = 0;
}


unsigned create_depth_render_buffer(unsigned xsize, unsigned ysize) {

	unsigned depthrenderbuffer(0);
	glGenRenderbuffers(1, &depthrenderbuffer);
	assert(depthrenderbuffer > 0);
	glBindRenderbuffer(GL_RENDERBUFFER, depthrenderbuffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, xsize, ysize);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthrenderbuffer);
	return depthrenderbuffer;
}


void disable_and_free_render_buffer(unsigned &render_buffer) {

	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	if (render_buffer > 0) {glDeleteRenderbuffers(1, &render_buffer);}
	render_buffer = 0;
}


void render_to_texture_t::pre_render(float xsize, float ysize, unsigned nx, unsigned ny, point const &center, vector3d const &view_dir) const {

	assert(xsize > 0.0 && ysize > 0);
	assert(tsize > 0 && nx > 0 && ny > 0);

	// setup matrices
	glViewport(0, 0, nx*tsize, ny*tsize);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(-xsize, xsize, -ysize, ysize, -(xsize + ysize), (xsize + ysize));
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	rotate_from_v2v(-plus_z, view_dir);
	translate_to(-center);

	// render
	glDisable(GL_LIGHTING);
}


void post_render() {

	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	//glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glEnable(GL_LIGHTING);
	disable_fbo();
	set_standard_viewport();
}


void set_temp_clear_color(colorRGBA const &clear_color) {

	colorRGBA orig_clear_color(BLACK);
	glGetFloatv(GL_COLOR_CLEAR_VALUE, (float *)&orig_clear_color);
	glClearColor_rgba(clear_color);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	glClearColor_rgba(orig_clear_color);
}


// Note: default viewing in -z dir
void render_to_texture_t::render(texture_pair_t &tpair, float xsize, float ysize, point const &center, vector3d const &view_dir,
	colorRGBA const &bkg_color, bool use_depth_buffer, bool mipmap)
{
	pre_render(xsize, ysize, 1, 1, center, view_dir); // setup matrices, etc.
	tpair.ensure_tid(tsize, mipmap);
	colorRGBA const clear_normal(0.5, 0.5, 0.5, 0.0);
	colorRGBA const clear_colors[2] = {bkg_color, clear_normal};

	for (unsigned d = 0; d < 2; ++d) { // {color, normal}
		unsigned fbo_id(0);
		enable_fbo(fbo_id, tpair.tids[d], 0); // too slow to create and free fbos every time?
		unsigned render_buffer(use_depth_buffer ? create_depth_render_buffer(tsize, tsize) : 0);
		set_temp_clear_color(clear_colors[d]);
		draw_geom(d != 0);
		if (use_depth_buffer) {disable_and_free_render_buffer(render_buffer);}
		free_fbo(fbo_id);
		if (mipmap) {build_texture_mipmaps(tpair.tids[d], 2);}
	}
	post_render(); // restore state
}


void render_to_texture_t::render(texture_atlas_t &atlas, float xsize, float ysize, point const &center, vector3d const &view_dir,
	colorRGBA const &bkg_color, bool use_depth_buffer, bool mipmap)
{
	assert(atlas.nx == 2 && atlas.ny == 1); // for now
	pre_render(atlas.nx*xsize, atlas.ny*ysize, atlas.nx, atlas.ny, center, view_dir); // setup matrices, etc.
	atlas.ensure_tid(tsize, mipmap);
	unsigned fbo_id(0);
	enable_fbo(fbo_id, atlas.tid, 0); // too slow to create and free fbos every time?
	unsigned render_buffer(use_depth_buffer ? create_depth_render_buffer(atlas.nx*tsize, atlas.ny*tsize) : 0);
	set_temp_clear_color(bkg_color); // FIXME: can only set a single clear color, should we draw a full quad to set the clear normal?
	vector3d xlate(2.0*xsize, 0.0, 0.0);
	rotate_vector3d_by_vr(-plus_z, view_dir, xlate);
	translate_to(-0.5*xlate);

	for (unsigned d = 0; d < 2; ++d) {
		draw_geom(d != 0);
		translate_to(xlate); // shift to next sub-texture region
	}
	if (use_depth_buffer) {disable_and_free_render_buffer(render_buffer);}
	free_fbo(fbo_id);
	if (mipmap) {build_texture_mipmaps(atlas.tid, 2);} // Note: if mipmapping is enabled, we should use a buffer region between the two sub-textures
	post_render(); // restore state
}


// ***************** Other *****************


bool gen_mipmaps(unsigned dim) {

	assert(dim >= 1 && dim <= 3);
	if (!glGenerateMipmap) return 0;
	int const tex_dims[3] = {GL_TEXTURE_1D, GL_TEXTURE_2D, GL_TEXTURE_3D};
	glGenerateMipmap(tex_dims[dim-1]);
	return 1;
}


