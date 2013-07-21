// 3D World - OpenGL CS184 Computer Graphics Project
// by Frank Gennari
// 3/10/02
#include "3DWorld.h"
#include "mesh.h"
#include "transform_obj.h"
#include "physics_objects.h"


unsigned const DEFAULT_MESH2D_SIZE((2*N_SPHERE_DIV)/3);


extern float base_gravity, tstep, fticks;
extern obj_type object_types[];


// *** xform_matrix ***


void xform_matrix::normalize() {

	for (unsigned i = 0; i < 3; ++i) { // renormalize matrix to account for fp error
		float const dist(sqrt(m[i+0]*m[i+0] + m[i+4]*m[i+4] + m[i+8]*m[i+8]));
		m[i+0] /= dist;
		m[i+4] /= dist;
		m[i+8] /= dist;
	}
}


void xform_matrix::load_identity() {

	for (unsigned i = 0; i < 16; ++i) {
		m[i] = float((i%5) == 0);
	}
}

void xform_matrix::rotate(float angle, vector3d const &rot) {

	// FIXME: do native rotation - see CREATE_ROT_MATRIX
	glPushMatrix();
	glLoadIdentity();
	rotate_about(angle, rot);
	apply();
	assign_mv_from_gl();
	glPopMatrix();
}

void xform_matrix::translate(vector3d const &t) {

	glPushMatrix();
	glLoadIdentity();
	translate_to(t);
	assign_mv_from_gl();
	glPopMatrix();
}

void xform_matrix::scale(vector3d const &s) {

	glPushMatrix();
	glLoadIdentity();
	scale_by(s);
	assign_mv_from_gl();
	glPopMatrix();
}


// *** mesh2d ***


void mesh2d::clear() {
	
	delete [] pmap;
	delete [] rmap;
	delete [] ptsh;
	pmap = NULL;
	rmap = NULL;
	ptsh = NULL;
	size = 0;
}


void mesh2d::set_size(unsigned sz) {

	assert(sz > 0);
	assert(size == 0 || sz == size);
	clear();
	size = sz;
}


template<typename T> void mesh2d::alloc_ptr(T *&p, T const val) {

	unsigned const num(get_num());
	delete [] p;
	p = new T[num];
	for (unsigned i = 0; i < num; ++i) p[i] = val;
}


void mesh2d::alloc_pmap() {alloc_ptr(pmap, 0.0f);}
void mesh2d::alloc_rmap() {alloc_ptr(rmap, bool(1));}
void mesh2d::alloc_emap() {alloc_ptr(emap, 0.0f);}
void mesh2d::alloc_ptsh() {alloc_ptr(ptsh, all_zeros);}


void mesh2d::reset_pmap() {

	if (!pmap) {alloc_pmap(); return;} // will be reset
	unsigned const num(get_num());
	for (unsigned i = 0; i < num; ++i) pmap[i] = 0.0;
}


void mesh2d::add_random(float mag, float min_mag, float max_mag, unsigned skipval) {

	if (!pmap) alloc_pmap();
	unsigned const num(get_num());

	for (unsigned i = (rand()%(skipval+1)); i < num; i += (skipval+1)) {
		pmap[i] = max(min_mag, min(max_mag, (pmap[i] + mag*signed_rand_float())));
	}
}


void mesh2d::mult_by(float val) {

	if (!pmap) return;
	unsigned const num(get_num());
	for (unsigned i = 0; i < num; ++i) pmap[i] *= val;
}


void mesh2d::unset_rand_rmap(unsigned num_remove) {

	if (!rmap) alloc_rmap();
	for (unsigned i = 0; i < num_remove; ++i) rmap[choose_rand()] = 0; // doesn't check for already removed elements
}


void mesh2d::set_rand_expand(float mag, unsigned num_exp) {

	if (!emap) alloc_emap();
	for (unsigned i = 0; i < num_exp; ++i) emap[choose_rand()] += mag; // doesn't check for already removed elements
}


void mesh2d::set_rand_translate(point const &tp, unsigned num_trans) {

	if (tp == all_zeros) return;
	if (!ptsh) alloc_ptsh();
	for (unsigned i = 0; i < num_trans; ++i) ptsh[choose_rand()] += tp; // doesn't check for already translated elements
}


void mesh2d::draw_perturbed_sphere(point const &pos, float radius, int ndiv, bool tex_coord) const {

	if (!pmap && !rmap && !emap && !ptsh && expand == 0.0) {
		draw_sphere_vbo(pos, radius, ndiv, 1);
	}
	else { // ndiv unused
		if (pmap || rmap || ptsh || emap) assert(size > 0);
		point const camera(get_camera_all());
		draw_subdiv_sphere(pos, radius, size, camera, pmap, tex_coord, 1, rmap, emap, ptsh, expand);
	}
}


// *** transform_data ***


void transform_data::set_perturb_size(unsigned i, unsigned sz) {

	assert(i < perturb_maps.size());

	if (perturb_maps[i].pmap) {
		assert(perturb_maps[i].get_size() == sz);
	}
	else {
		perturb_maps[i].set_size(sz);
		perturb_maps[i].alloc_pmap();
	}
}


void transform_data::add_rand_perturb(unsigned i, float mag, float min_mag, float max_mag) {
	
	assert(i < perturb_maps.size());
	set_perturb_size(i, DEFAULT_MESH2D_SIZE);
	perturb_maps[i].add_random(mag, min_mag, max_mag);
}


void transform_data::add_perturb_at(unsigned s, unsigned t, unsigned i, float val, float min_mag, float max_mag) {

	assert(i < perturb_maps.size());
	perturb_maps[i].set_val(s, t, max(min_mag, min(max_mag, (perturb_maps[i].get_val(s, t) + val))));
}


void transform_data::reset_perturb_if_set(unsigned i) {
	
	assert(i < perturb_maps.size());
	if (perturb_maps[i].pmap) perturb_maps[i].reset_pmap();
}


transform_data::~transform_data() {
		
	for (unsigned i = 0; i < perturb_maps.size(); ++i) {
		perturb_maps[i].clear();
	}
}


// *** deformation code ***


void apply_obj_mesh_roll(xform_matrix &matrix, point const &pos, point const &lpos, float radius, float a_add, float a_mult) {

	if (pos != lpos) {
		int const xpos(get_xpos(pos.x)), ypos(get_ypos(pos.y));

		if (!point_outside_mesh(xpos, ypos)) {
			vector3d const delta(pos, lpos);
			double const dmag(delta.mag()), angle(a_mult*(360.0*dmag/(TWO_PI*radius)) + a_add);
			vector3d const vrot(cross_product(surface_normals[ypos][xpos], delta/dmag));
			float const vrm(vrot.mag());

			if (vrm > TOLERANCE) {
				matrix.normalize();
				matrix.rotate(angle, vrot);
			}
		}
	}
	matrix.apply();
}


void deform_obj(dwobject &obj, vector3d const &norm, vector3d const &v0) { // apply collision deformations

	float const deform(object_types[obj.type].deform);
	if (deform == 0.0) return;
	assert(deform > 0.0 && deform < 1.0);
	vector3d const vd(obj.velocity, v0);
	float const vthresh(base_gravity*GRAVITY*tstep*object_types[obj.type].gravity), vd_mag(vd.mag());

	if (vd_mag > max(2.0f*vthresh, 12.0f/fticks) && (fabs(v0.x) + fabs(v0.y)) > 0.01) { // what about when it hits the ground/mesh?
		float const deform_mag(SQRT3*deform*min(1.0, 0.05*vd_mag));
		
		for (unsigned d = 0; d < 3; ++d) {
			obj.vdeform[d] -= fabs(norm[d])*deform_mag;
		}
		obj.vdeform *= SQRT3/obj.vdeform.mag(); // normalize the volume
		float vdmin(1.0 - deform);

		for (unsigned d = 0; d < 3; ++d) {
			vdmin = min(vdmin, obj.vdeform[d]);
		}
		if (vdmin < (1.0 - deform)) {
			for (unsigned d = 0; d < 3; ++d) {
				obj.vdeform[d] += ((1.0 - deform) - vdmin);
			}
			obj.vdeform *= SQRT3/obj.vdeform.mag(); // re-normalize
		}
	}
}


void update_deformation(dwobject &obj) {

	if (obj.vdeform != all_ones && object_types[obj.type].def_recover > 0.0) {
		obj.vdeform += all_ones*(fticks*object_types[obj.type].def_recover);
		obj.vdeform *= SQRT3/obj.vdeform.mag(); // normalize the volume
	}
}



