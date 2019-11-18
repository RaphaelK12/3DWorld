// 3D World - Building Interior Generation
// by Frank Gennari 11/15/19

#include "3DWorld.h"
#include "function_registry.h"
#include "buildings.h"

extern building_params_t global_building_params;


void building_t::set_z_range(float z1, float z2) {
	bcube.z1() = z1; bcube.z2() = z2;
	adjust_part_zvals_for_floor_spacing(bcube);
	if (!parts.empty()) {parts[0].z1() = z1; parts[0].z2() = z2;}
}
building_mat_t const &building_t::get_material() const {return global_building_params.get_material(mat_ix);}

void building_t::split_in_xy(cube_t const &seed_cube, rand_gen_t &rgen) {

	// generate L, T, U, H, + shape
	point const llc(seed_cube.get_llc()), sz(seed_cube.get_size());
	int const shape(rand()%9); // 0-8
	bool const is_hp(shape >= 7);
	bool const dim(rgen.rand_bool()); // {x,y}
	bool const dir(is_hp ? 1 : rgen.rand_bool()); // {neg,pos} - H/+ shapes are always pos
	float const div(is_hp ? rgen.rand_uniform(0.2, 0.4) : rgen.rand_uniform(0.3, 0.7)), s1(rgen.rand_uniform(0.2, 0.4)), s2(rgen.rand_uniform(0.6, 0.8)); // split pos in 0-1 range
	float const dpos(llc[dim] + div*sz[dim]), spos1(llc[!dim] + s1*sz[!dim]), spos2(llc[!dim] + s2*sz[!dim]); // split pos in cube space
	unsigned const start(parts.size()), num((shape >= 6) ? 3 : 2);
	parts.resize(start+num, seed_cube);
	parts[start+0].d[dim][ dir] = dpos; // full width part (except +)
	parts[start+1].d[dim][!dir] = dpos; // partial width part (except +)

	switch (shape) {
	case 0: case 1: case 2: case 3: // L
		parts[start+1].d[!dim][shape>>1] = ((shape&1) ? spos2 : spos1);
		break;
	case 4: case 5: // T
		parts[start+1].d[!dim][0] = spos1;
		parts[start+1].d[!dim][1] = spos2;
		break;
	case 6: // U
		parts[start+2].d[ dim][!dir] = dpos; // partial width part
		parts[start+1].d[!dim][1   ] = spos1;
		parts[start+2].d[!dim][0   ] = spos2;
		break;
	case 7: { // H
		float const dpos2(llc[dim] + (1.0 - div)*sz[dim]); // other end
		parts[start+1].d[ dim][ dir] = dpos2;
		parts[start+1].d[!dim][ 0  ] = spos1; // middle part
		parts[start+1].d[!dim][ 1  ] = spos2;
		parts[start+2].d[ dim][!dir] = dpos2; // full width part
		break;
	}
	case 8: { // +
		float const dpos2(llc[dim] + (1.0 - div)*sz[dim]); // other end
		parts[start+0].d[!dim][ 0  ] = spos1;
		parts[start+0].d[!dim][ 1  ] = spos2;
		parts[start+2].d[!dim][ 0  ] = spos1;
		parts[start+2].d[!dim][ 1  ] = spos2;
		parts[start+1].d[ dim][ dir] = dpos2; // middle part
		parts[start+2].d[ dim][!dir] = dpos2; // partial width part
		break;
	}
	default: assert(0);
	}
}

void building_t::gen_rotation(rand_gen_t &rgen) {

	float const max_rot_angle(get_material().max_rot_angle);
	if (max_rot_angle == 0.0) return;
	float const rot_angle(rgen.rand_uniform(0.0, max_rot_angle));
	rot_sin = sin(rot_angle);
	rot_cos = cos(rot_angle);
	parts.clear();
	parts.push_back(bcube); // this is the actual building base
	cube_t const &bc(parts.back());
	point const center(bc.get_cube_center());

	for (unsigned i = 0; i < 4; ++i) {
		point corner(bc.d[0][i&1], bc.d[1][i>>1], bc.d[2][i&1]);
		do_xy_rotate(rot_sin, rot_cos, center, corner);
		if (i == 0) {bcube.set_from_point(corner);} else {bcube.union_with_pt(corner);} // Note: detail cubes are excluded
	}
}

bool building_t::check_bcube_overlap_xy(building_t const &b, float expand_rel, float expand_abs, vector<point> &points) const {

	if (expand_rel == 0.0 && expand_abs == 0.0 && !bcube.intersects(b.bcube)) return 0;
	if (!is_rotated() && !b.is_rotated()) return 1; // above check is exact, top-level bcube check up to the caller
	if (b.bcube.contains_pt_xy(bcube.get_cube_center()) || bcube.contains_pt_xy(b.bcube.get_cube_center())) return 1; // slightly faster to include this check
	return (check_bcube_overlap_xy_one_dir(b, expand_rel, expand_abs, points) || b.check_bcube_overlap_xy_one_dir(*this, expand_rel, expand_abs, points));
}

// Note: only checks for point (x,y) value contained in one cube/N-gon/cylinder; assumes pt has already been rotated into local coordinate frame
bool building_t::check_part_contains_pt_xy(cube_t const &part, point const &pt, vector<point> &points) const {

	if (!part.contains_pt_xy(pt)) return 0; // check bounding cube
	if (is_simple_cube()) return 1; // that's it
	building_draw_utils::calc_poly_pts(*this, part, points);
	return point_in_polygon_2d(pt.x, pt.y, &points.front(), points.size(), 0, 1); // 2D x/y containment
}

bool building_t::check_bcube_overlap_xy_one_dir(building_t const &b, float expand_rel, float expand_abs, vector<point> &points) const { // can be called before levels/splits are created

	// Note: easy cases are handled by check_bcube_overlap_xy() above
	point const center1(b.bcube.get_cube_center()), center2(bcube.get_cube_center());

	for (auto p1 = b.parts.begin(); p1 != b.parts.end(); ++p1) {
		point pts[9]; // {center, 00, 10, 01, 11, x0, x1, y0, y1}

		if (b.parts.size() == 1) {pts[0] = center1;} // single cube: we know we're rotating about its center
		else {
			pts[0] = p1->get_cube_center();
			do_xy_rotate(b.rot_sin, b.rot_cos, center1, pts[0]); // rotate into global space
		}
		cube_t c_exp(*p1);
		c_exp.expand_by_xy(expand_rel*p1->get_size() + vector3d(expand_abs, expand_abs, expand_abs));

		for (unsigned i = 0; i < 4; ++i) { // {00, 10, 01, 11}
			pts[i+1].assign(c_exp.d[0][i&1], c_exp.d[1][i>>1], 0.0); // XY only
			do_xy_rotate(b.rot_sin, b.rot_cos, center1, pts[i+1]); // rotate into global space
		}
		for (unsigned i = 0; i < 5; ++i) {do_xy_rotate(-rot_sin, rot_cos, center2, pts[i]);} // inverse rotate into local coord space - negate the sine term
		cube_t c_exp_rot(pts+1, 4); // use points 1-4
		pts[5] = 0.5*(pts[1] + pts[3]); // x0 edge center
		pts[6] = 0.5*(pts[2] + pts[4]); // x1 edge center
		pts[7] = 0.5*(pts[1] + pts[2]); // y0 edge center
		pts[8] = 0.5*(pts[3] + pts[4]); // y1 edge center

		for (auto p2 = parts.begin(); p2 != parts.end(); ++p2) {
			if (c_exp_rot.contains_pt_xy(p2->get_cube_center())) return 1; // quick and easy test for heavy overlap

			for (unsigned i = 0; i < 9; ++i) {
				if (check_part_contains_pt_xy(*p2, pts[i], points)) return 1; // Note: building geometry is likely not yet generated, this check should be sufficient
				//if (p2->contains_pt_xy(pts[i])) return 1;
			}
		}
	} // for p1
	return 0;
}

bool building_t::test_coll_with_sides(point &pos, point const &p_last, float radius, cube_t const &part, vector<point> &points, vector3d *cnorm) const {

	building_draw_utils::calc_poly_pts(*this, part, points); // without the expand
	point quad_pts[4]; // quads
	bool updated(0);

	// FIXME: if the player is moving too quickly, the intersection with a side polygon may be missed,
	// which allows the player to travel through the building, but using a line intersection test from p_past2 to pos has other problems
	for (unsigned S = 0; S < num_sides; ++S) { // generate vertex data quads
		for (unsigned d = 0, ix = 0; d < 2; ++d) {
			point const &p(points[(S+d)%num_sides]);
			for (unsigned e = 0; e < 2; ++e) {quad_pts[ix++].assign(p.x, p.y, part.d[2][d^e]);}
		}
		vector3d const normal(get_poly_norm(quad_pts));
		float const rdist(dot_product_ptv(normal, pos, quad_pts[0]));
		if (rdist < 0.0 || rdist >= radius) continue; // too far or wrong side
		if (!sphere_poly_intersect(quad_pts, 4, pos, normal, rdist, radius)) continue;
		pos += normal*(radius - rdist);
		if (cnorm) {*cnorm = normal;}
		updated = 1;
	} // for S
	if (updated) return 1;

	if (max(pos.z, p_last.z) > part.z2() && point_in_polygon_2d(pos.x, pos.y, &points.front(), num_sides, 0, 1)) { // test top plane (sphere on top of polygon?)
		pos.z = part.z2() + radius; // make sure it doesn't intersect the roof
		if (cnorm) {*cnorm = plus_z;}
		return 1;
	}
	return 0;
}

bool building_t::check_sphere_coll(point &pos, point const &p_last, vector3d const &xlate, float radius,
	bool xy_only, vector<point> &points, vector3d *cnorm_ptr, bool check_interior) const
{
	if (!is_valid()) return 0; // invalid building
	point p_int;
	vector3d cnorm; // unused
	unsigned cdir(0); // unused
	if (radius > 0.0 && !sphere_cube_intersect(pos, radius, (bcube + xlate), p_last, p_int, cnorm, cdir, 1, xy_only)) return 0;
	point pos2(pos), p_last2(p_last), center;
	bool had_coll(0), is_interior(0);

	if (is_rotated()) {
		center = bcube.get_cube_center() + xlate;
		do_xy_rotate(-rot_sin, rot_cos, center, pos2); // inverse rotate - negate the sine term
		do_xy_rotate(-rot_sin, rot_cos, center, p_last2);
	}
	for (auto i = parts.begin(); i != parts.end(); ++i) {
		if (xy_only && i->d[2][0] > bcube.d[2][0]) break; // only need to check first level in this mode
		if (!xy_only && ((pos2.z + radius < i->d[2][0] + xlate.z) || (pos2.z - radius > i->d[2][1] + xlate.z))) continue; // test z overlap
		if (radius == 0.0 && !(xy_only ? i->contains_pt_xy(pos2) : i->contains_pt(pos2))) continue; // no intersection; ignores p_last

		if (use_cylinder_coll()) {
			point const cc(i->get_cube_center() + xlate);
			float const crx(0.5*i->get_dx()), cry(0.5*i->get_dy()), r_sum(radius + max(crx, cry));
			if (!dist_xy_less_than(pos2, cc, r_sum)) continue; // no intersection

			if (fabs(crx - cry) < radius) { // close to a circle
				if (p_last2.z > i->d[2][1] + xlate.z && dist_xy_less_than(pos2, cc, max(crx, cry))) {
					pos2.z = i->z2() + radius; // make sure it doesn't intersect the roof
					if (cnorm_ptr) {*cnorm_ptr = plus_z;}
				}
				else { // side coll
					vector2d const d((pos2.x - cc.x), (pos2.y - cc.y));
					float const mult(r_sum/d.mag());
					pos2.x = cc.x + mult*d.x;
					pos2.y = cc.y + mult*d.y;
					if (cnorm_ptr) {*cnorm_ptr = vector3d(d.x, d.y, 0.0).get_norm();} // no z-component
				}
				had_coll = 1;
			}
			else {
				had_coll |= test_coll_with_sides(pos2, p_last2, radius, (*i + xlate), points, cnorm_ptr); // use polygon collision test
			}
		}
		else if (num_sides != 4) { // triangle, hexagon, octagon, etc.
			had_coll |= test_coll_with_sides(pos2, p_last2, radius, (*i + xlate), points, cnorm_ptr);
		}
		else if (sphere_cube_int_update_pos(pos2, radius, (*i + xlate), p_last2, 1, xy_only, cnorm_ptr)) { // cube
			had_coll = 1; // flag as colliding, continue to look for more collisions (inside corners)
			if (check_interior && interior != nullptr) {is_interior = 1;}
		}
	} // for i
	if (!xy_only) { // don't need to check details and roof in xy_only mode because they're contained in the XY footprint of the parts
		for (auto i = details.begin(); i != details.end(); ++i) {
			if (sphere_cube_int_update_pos(pos2, radius, (*i + xlate), p_last2, 1, xy_only, cnorm_ptr)) {had_coll = 1;} // cube, flag as colliding
		}
		for (auto i = roof_tquads.begin(); i != roof_tquads.end(); ++i) { // Note: doesn't really work with a pointed roof
			point const pos_xlate(pos2 - xlate);
			vector3d const normal(i->get_norm());
			float const rdist(dot_product_ptv(normal, pos_xlate, i->pts[0]));

			if (fabs(rdist) < radius && sphere_poly_intersect(i->pts, i->npts, pos_xlate, normal, rdist, radius)) {
				pos2 += normal*(radius - rdist); // update current pos
				had_coll = 1; // flag as colliding
				if (cnorm_ptr) {*cnorm_ptr = ((normal.z < 0.0) ? -1.0 : 1.0)*normal;} // make sure normal points up
				break; // only use first colliding tquad
			}
		}
	}
	if (is_interior) {had_coll = check_sphere_coll_interior(pos, p_last, xlate, radius, xy_only, cnorm_ptr);} // sphere collides with cube and check_interior=1
	if (!had_coll) return 0; // Note: no collisions with windows or doors, since they're colinear with walls

	if (is_rotated()) {
		do_xy_rotate(rot_sin, rot_cos, center, pos2); // rotate back around center
		if (cnorm_ptr) {do_xy_rotate(rot_sin, rot_cos, all_zeros, *cnorm_ptr);} // rotate back (pure rotation)
	}
	pos = pos2;
	return had_coll;
}

// Note: pos and p_last are already in rotated coordinate space
bool building_t::check_sphere_coll_interior(point &pos, point const &p_last, vector3d const &xlate, float radius, bool xy_only, vector3d *cnorm) const {
	assert(interior);
	bool had_coll(0);

	for (unsigned d = 0; d < 2; ++d) { // check XY collision with walls
		for (auto i = interior->walls[d].begin(); i != interior->walls[d].end(); ++i) {
			had_coll |= sphere_cube_int_update_pos(pos, radius, (*i + xlate), p_last, 1, 1, cnorm); // skip_z=1
		}
	}
	if (!xy_only) { // check Z collision with floors; no need to check ceilings
		for (auto i = interior->floors.begin(); i != interior->floors.end(); ++i) {
			had_coll |= sphere_cube_int_update_pos(pos, radius, (*i + xlate), p_last, 1, xy_only, cnorm);
		}
	}
	if (interior->room_geom) { // collision with room cubes; XY only?
		vector<colored_cube_t> const &cubes(interior->room_geom->cubes);
		for (auto c = cubes.begin(); c != cubes.end(); ++c) {had_coll |= sphere_cube_int_update_pos(pos, radius, (*c + xlate), p_last, 1, 1, cnorm);} // skip_z=1???
	}
	return had_coll;
}

unsigned building_t::check_line_coll(point const &p1, point const &p2, vector3d const &xlate, float &t, vector<point> &points,
	bool occlusion_only, bool ret_any_pt, bool no_coll_pt) const
{
	if (!check_line_clip(p1-xlate, p2-xlate, bcube.d)) return 0; // no intersection
	point p1r(p1), p2r(p2);
	float tmin(0.0), tmax(1.0);
	unsigned coll(0); // 0=none, 1=side, 2=roof, 3=details

	if (is_rotated()) {
		point const center(bcube.get_cube_center() + xlate);
		do_xy_rotate(-rot_sin, rot_cos, center, p1r); // inverse rotate - negate the sine term
		do_xy_rotate(-rot_sin, rot_cos, center, p2r);
	}
	p1r -= xlate; p2r -= xlate;
	float const pzmin(min(p1r.z, p2r.z)), pzmax(max(p1r.z, p2r.z));
	bool const vert(p1r.x == p2r.x && p1r.y == p2r.y);

	for (auto i = parts.begin(); i != parts.end(); ++i) {
		if (pzmin > i->z2() || pzmax < i->z1()) continue; // no overlap in z
		bool hit(0);

		if (use_cylinder_coll()) { // vertical cylinder
			// Note: we know the line intersects the cylinder's bcube, and there's a good chance it intersects the cylinder, so we don't need any expensive early termination cases here
			point const cc(i->get_cube_center());
			vector3d const csz(i->get_size());

			if (vert) { // vertical line + vertical cylinder optimization + handling of ellipsoids
				if (!point_in_ellipse(p1r, cc, 0.5*csz.x, 0.5*csz.y)) continue; // no intersection (below test should return true as well)
				tmin = (i->z2() - p1r.z)/(p2r.z - p1r.z);
				if (tmin >= 0.0 && tmin < t) {t = tmin; hit = 1;}
			}
			else {
				float const radius(0.5*(occlusion_only ? min(csz.x, csz.y) : max(csz.x, csz.y))); // use conservative radius unless this is an occlusion query
				point const cp1(cc.x, cc.y, i->z1()), cp2(cc.x, cc.y, i->z2());
				if (!line_int_cylinder(p1r, p2r, cp1, cp2, radius, radius, 1, tmin) || tmin > t) continue; // conservative for non-occlusion rays

				if (!occlusion_only && csz.x != csz.y) { // ellipse
					vector3d const delta(p2r - p1r);
					float const rx_inv_sq(1.0/(0.25*csz.x*csz.x)), ry_inv_sq(1.0/(0.25*csz.y*csz.y));
					float t_step(0.1*max(csz.x, csz.y)/delta.mag());

					for (unsigned n = 0; n < 10; ++n) { // use an interative approach
						if (point_in_ellipse_risq((p1r + tmin*delta), cc, rx_inv_sq, ry_inv_sq)) {hit = 1; tmin -= t_step;} else {tmin += t_step;}
						if (hit) {t_step *= 0.5;} // converge on hit point
					}
					if (!hit) continue; // not actually a hit
				} // end ellipse case
				t = tmin; hit = 1;
			}
		}
		else if (num_sides != 4) {
			building_draw_utils::calc_poly_pts(*this, *i, points);
			float const tz((i->z2() - p1r.z)/(p2r.z - p1r.z)); // t value at zval = top of cube

			if (tz >= 0.0 && tz < t) {
				float const xval(p1r.x + tz*(p2r.x - p1r.x)), yval(p1r.y + tz*(p2r.y - p1r.y));
				if (point_in_polygon_2d(xval, yval, &points.front(), points.size(), 0, 1)) {t = tz; hit = 1;} // XY plane test for vertical lines and top surface
			}
			if (!vert) { // test building sides
				point quad_pts[4]; // quads

				for (unsigned S = 0; S < num_sides; ++S) { // generate vertex data quads
					for (unsigned d = 0, ix = 0; d < 2; ++d) {
						point const &p(points[(S+d)%num_sides]);
						for (unsigned e = 0; e < 2; ++e) {quad_pts[ix++].assign(p.x, p.y, i->d[2][d^e]);}
					}
					if (line_poly_intersect(p1r, p2r, quad_pts, 4, get_poly_norm(quad_pts), tmin) && tmin < t) {t = tmin; hit = 1;} // Note: untested
				} // for S
			}
		}
		else if (get_line_clip(p1r, p2r, i->d, tmin, tmax) && tmin < t) {t = tmin; hit = 1;} // cube

		if (hit) {
			if (occlusion_only) return 1; // early exit
			if (vert) {coll = 2;} // roof
			else {
				float const zval(p1.z + t*(p2.z - p1.z));
				coll = ((fabs(zval - i->d[2][1]) < 0.0001*i->get_dz()) ? 2 : 1); // test if clipped zval is close to the roof zval
			}
			if (ret_any_pt) return coll;
		}
	} // for i
	if (occlusion_only) return 0;

	for (auto i = details.begin(); i != details.end(); ++i) {
		if (get_line_clip(p1r, p2r, i->d, tmin, tmax) && tmin < t) {t = tmin; coll = 3;} // details cube
	}
	if (!no_coll_pt || !vert) { // vert line already tested building cylins/cubes, and marked coll roof, no need to test again unless we need correct coll_pt t-val
		for (auto i = roof_tquads.begin(); i != roof_tquads.end(); ++i) {
			if (line_poly_intersect(p1r, p2r, i->pts, i->npts, i->get_norm(), tmin) && tmin < t) {t = tmin; coll = 2;} // roof quad
		}
	}
	return coll; // Note: no collisions with windows or doors, since they're colinear with walls; no collision with interior for now
}

// Note: if xy_radius == 0.0, this is a point test; otherwise, it's an approximate vertical cylinder test
bool building_t::check_point_or_cylin_contained(point const &pos, float xy_radius, vector<point> &points) const {

	if (xy_radius == 0.0 && !bcube.contains_pt(pos)) return 0; // no intersection
	point pr(pos);
	if (is_rotated()) {do_xy_rotate(-rot_sin, rot_cos, bcube.get_cube_center(), pr);} // inverse rotate - negate the sine term

	for (auto i = parts.begin(); i != parts.end(); ++i) {
		if (pr.z > i->z2() || pr.z < i->z1()) continue; // no overlap in z

		if (use_cylinder_coll()) { // vertical cylinder
			point const cc(i->get_cube_center());
			vector3d const csz(i->get_size());
			float const dx(cc.x - pr.x), dy(cc.y - pr.y), rx(0.5*csz.x + xy_radius), ry(0.5*csz.y + xy_radius);
			if (dx*dx/(rx*rx) + dy*dy/(ry*ry) > 1.0f) continue; // no intersection (below test should return true as well)
			return 1;
		}
		else if (num_sides != 4) {
			building_draw_utils::calc_poly_pts(*this, *i, points);

			if (xy_radius > 0.0) { // cylinder case: expand polygon by xy_radius; assumes a convex polygon
				point const center(i->get_cube_center());

				for (auto p = points.begin(); p != points.end(); ++p) {
					vector3d dir(*p - center);
					dir.z = 0.0; // only want XY component
					*p += dir*(xy_radius/dir.mag());
				}
			}
			if (point_in_polygon_2d(pr.x, pr.y, &points.front(), points.size(), 0, 1)) return 1; // XY plane test for top surface
		}
		else { // cube
			if (xy_radius > 0.0) {
				cube_t cube(*i);
				cube.expand_by(xy_radius);
				if (cube.contains_pt(pr)) return 1;
			}
			else if (i->contains_pt(pr)) return 1;
		}
	} // for i
	return 0;
}

void building_t::calc_bcube_from_parts() {
	assert(!parts.empty());
	bcube = parts[0];
	for (auto i = parts.begin()+1; i != parts.end(); ++i) {bcube.union_with_cube(*i);} // update bcube
}

void building_t::adjust_part_zvals_for_floor_spacing(cube_t &c) const {

	if (!EXACT_MULT_FLOOR_HEIGHT) return;
	float const floor_spacing(get_material().get_floor_spacing()), dz(c.dz());
	assert(dz > 0.0 && floor_spacing > 0.0);
	float const num_floors(dz/floor_spacing);
	int const targ_num_floors(max(1, round_fp(num_floors)));
	c.z2() += floor_spacing*(targ_num_floors - num_floors); // ensure c.dz() is an exact multiple of num_floors
}

void building_t::gen_geometry(int rseed1, int rseed2) {

	if (!is_valid()) return; // invalid building
	if (!parts.empty()) {adjust_part_zvals_for_floor_spacing(parts.front());}
	cube_t const base(parts.empty() ? bcube : parts.back());
	assert(base.is_strictly_normalized());
	parts.clear();
	details.clear();
	roof_tquads.clear();
	doors.clear();
	interior.reset();
	building_mat_t const &mat(get_material());
	rand_gen_t rgen;
	rgen.set_state(123+rseed1, 345*rseed2);
	ao_bcz2 = bcube.z2(); // capture z2 before union with roof and detail geometry (which increases building height)
	if (is_house) {gen_house(base, rgen); return;}

	// determine building shape (cube, cylinder, other)
	if (rgen.rand_probability(mat.round_prob)) {num_sides = MAX_CYLIN_SIDES;} // max number of sides for drawing rounded (cylinder) buildings
	else if (rgen.rand_probability(mat.cube_prob)) {num_sides = 4;} // cube
	else { // N-gon
		num_sides = mat.min_sides;
		if (mat.min_sides != mat.max_sides) {num_sides += (rgen.rand() % (1 + abs((int)mat.max_sides - (int)mat.min_sides)));}
	}
	bool const was_cube(is_cube()); // before num_sides increase due to ASF

	if (num_sides >= 6 && mat.max_fsa > 0.0) { // at least 6 sides
		flat_side_amt = max(0.0f, min(0.45f, rgen.rand_uniform(mat.min_fsa, mat.max_fsa)));
		if (flat_side_amt > 0.0 && rot_sin == 0.0) {start_angle = rgen.rand_uniform(0.0, TWO_PI);} // flat side, not rotated: add random start angle to break up uniformity
	}
	if ((num_sides == 3 || num_sides == 4 || num_sides == 6) && mat.max_asf > 0.0 && rgen.rand_probability(mat.asf_prob)) { // triangles/cubes/hexagons
		alt_step_factor = max(0.0f, min(0.99f, rgen.rand_uniform(mat.min_asf, mat.max_asf)));
		if (alt_step_factor > 0.0 && !(num_sides&1)) {half_offset = 1;} // chamfered cube/hexagon
		if (alt_step_factor > 0.0) {num_sides *= 2;}
	}

	// determine the number of levels and splits
	unsigned num_levels(mat.min_levels);

	if (mat.min_levels < mat.max_levels) { // have a range of levels
		if (was_cube || rgen.rand_bool()) {num_levels += rgen.rand() % (mat.max_levels - mat.min_levels + 1);} // only half of non-cubes are multilevel (unless min_level > 1)
	}
	if (mat.min_level_height > 0.0) {num_levels = max(mat.min_levels, min(num_levels, unsigned(bcube.get_size().z/mat.min_level_height)));}
	num_levels = max(num_levels, 1U); // min_levels can be zero to apply more weight to 1 level buildings
	bool const do_split(num_levels < 4 && is_cube() && rgen.rand_probability(mat.split_prob)); // don't split buildings with 4 or more levels, or non-cubes

	if (num_levels == 1) { // single level
		if (do_split) {split_in_xy(base, rgen);} // generate L, T, or U shape
		else { // single part, entire cube/cylinder
			parts.push_back(base);
			if ((rgen.rand()&3) != 0) {gen_sloped_roof(rgen);} // 75% chance
			gen_details(rgen);
		}
		gen_interior(rgen, 0);
		gen_building_doors_if_needed(rgen);
		return; // for now the bounding cube
	}
	// generate building levels and splits
	parts.resize(num_levels);
	float const height(base.get_dz()), dz(height/num_levels);
	assert(height > 0.0);

	if (!do_split && (rgen.rand()&3) < (was_cube ? 2 : 3)) { // oddly shaped multi-sided overlapping sections (50% chance for cube buildings and 75% chance for others)
		point const llc(base.get_llc()), sz(base.get_size());

		for (unsigned i = 0; i < num_levels; ++i) { // generate overlapping cube levels
			cube_t &bc(parts[i]);
			bc.z1() = base.z1(); // z1
			bc.z2() = base.z1() + (i+1)*dz; // z2
			if (i > 0) {bc.z2() += dz*rgen.rand_uniform(-0.5, 0.5); bc.z2() = min(bc.z2(), base.z2());}
			adjust_part_zvals_for_floor_spacing(bc);
			float const min_edge_mode(mat.no_city ? 0.04*i : 0.0); // prevent z-fighting on non-city building windows (stretched texture)

			for (unsigned n = 0; n < 10; ++n) { // make 10 attempts to generate a cube that doesn't contain any existing cubes (can occasionally still fail)
				for (unsigned d = 0; d < 2; ++d) { // x,y
					bc.d[d][0] = base.d[d][0] + max(rgen.rand_uniform(-0.2, 0.45), min_edge_mode)*sz[d];
					bc.d[d][1] = base.d[d][1] - max(rgen.rand_uniform(-0.2, 0.45), min_edge_mode)*sz[d];
				}
				assert(bc.is_strictly_normalized());
				bool contains(0);
				for (unsigned j = 0; j < i; ++j) {contains |= bc.contains_cube(parts[j]);}
				if (!contains) break; // success
			} // for n
		} // for i
		calc_bcube_from_parts(); // update bcube
		gen_details(rgen);
		gen_interior(rgen, 1);
		gen_building_doors_if_needed(rgen);
		return;
	}
	for (unsigned i = 0; i < num_levels; ++i) {
		cube_t &bc(parts[i]);
		if (i == 0) {bc = base;} // use full building footprint
		else {
			cube_t const &prev(parts[i-1]);
			float const shift_mult(was_cube ? 1.0 : 0.5); // half the shift for non-cube buildings

			for (unsigned d = 0; d < 2; ++d) {
				float const len(prev.d[d][1] - prev.d[d][0]), min_edge_len((0.2f/shift_mult)*(bcube.d[d][1] - bcube.d[d][0]));
				bool const inv(rgen.rand_bool());

				for (unsigned e = 0; e < 2; ++e) {
					float delta(0.0);
					if (rgen.rand()&3) {delta = shift_mult*rgen.rand_uniform(0.1, 0.4);} // 25% chance of no shift, 75% chance of 20-40% shift
					bc.d[d][e] = prev.d[d][e] + (e ? -delta : delta)*len;
				}
				for (unsigned E = 0; E < 2; ++E) {
					bool const e((E != 0) ^ inv); // no dir favoritism for 20% check
					if (bc.d[d][1] - bc.d[d][0] < min_edge_len) {bc.d[d][e] = prev.d[d][e];} // if smaller than 20% base width, revert the change
				}
			}
			bc.z1() = prev.z2(); // z1
		}
		bc.z2() = bc.z1() + dz; // z2
		bc.normalize(); // handle XY inversion due to shift
	} // for i
	for (unsigned i = 1; i < num_levels; ++i) {
		float const ddz(rgen.rand_uniform(-0.35*dz, 0.35*dz)); // random shift in z height
		parts[i-1].z2() += ddz;
		adjust_part_zvals_for_floor_spacing(parts[i-1]);
		parts[i].z1() = parts[i-1].z2(); // make top and bottom parts align
	}
	adjust_part_zvals_for_floor_spacing(parts[num_levels-1]); // last one
	max_eq(bcube.z2(), parts[num_levels-1].z2()); // adjust bcube if needed

	if (do_split) { // generate L, T, or U shape
		cube_t const split_cube(parts.back());
		parts.pop_back();
		split_in_xy(split_cube, rgen);
	}
	else {
		if ((rgen.rand()&3) != 0) {gen_sloped_roof(rgen);} // 67% chance
		if (num_levels <= 3) {gen_details(rgen);}
	}
	gen_interior(rgen, 0);
	gen_building_doors_if_needed(rgen);
}

bool get_largest_xy_dim(cube_t const &c) {return (c.dy() > c.dx());}

cube_t building_t::place_door(cube_t const &base, bool dim, bool dir, float door_height, float door_center, float door_pos, float door_center_shift, float width_scale, rand_gen_t &rgen) {

	if (door_center == 0.0) { // door not yet calculated; add door to first part of house
		// TODO_INT: if (interior) {interior->walls[dim]...}
		bool const centered(door_center_shift == 0.0 || hallway_dim == (unsigned char)dim); // center doors connected to primary hallways
		float const offset(centered ? 0.5 : rgen.rand_uniform(0.5-door_center_shift, 0.5+door_center_shift));
		door_center = offset*base.d[!dim][0] + (1.0 - offset)*base.d[!dim][1];
		door_pos    = base.d[dim][dir];
	}
	float const door_half_width(0.5*width_scale*door_height);
	float const door_shift((is_house ? 0.005 : 0.001)*base.dz());
	cube_t door;
	door.z1() = base.z1(); // same bottom as house
	door.z2() = door.z1() + door_height;
	door.d[ dim][!dir] = door_pos + door_shift*(dir ? 1.0 : -1.0); // move slightly away from the house to prevent z-fighting
	door.d[ dim][ dir] = door.d[dim][!dir]; // make zero size in this dim
	door.d[!dim][0] = door_center - door_half_width; // left
	door.d[!dim][1] = door_center + door_half_width; // right
	return door;
}

void building_t::gen_house(cube_t const &base, rand_gen_t &rgen) {

	assert(parts.empty());
	int const type(rgen.rand()%3); // 0=single cube, 1=L-shape, 2=two-part
	bool const two_parts(type != 0);
	unsigned force_dim[2] = {2}; // force roof dim to this value, per part; 2 = unforced/auto
	bool skip_last_roof(0);
	num_sides = 4;
	parts.reserve(two_parts ? 5 : 2); // two house sections + porch roof + porch support + chimney (upper bound)
	parts.push_back(base);
	// add a door
	bool const gen_door(global_building_params.windows_enabled());
	float door_height(get_door_height()), door_center(0.0), door_pos(0.0);
	bool door_dim(rgen.rand_bool()), door_dir(0);
	unsigned door_part(0);

	if (two_parts) { // multi-part house
		parts.push_back(base); // add second part
		bool const dir(rgen.rand_bool()); // in dim
		float const split(rgen.rand_uniform(0.4, 0.6)*(dir  ? -1.0 : 1.0));
		float delta_height(0.0), shrink[2] = {0.0};
		bool dim(0), dir2(0);

		if (type == 1) { // L-shape
			dir2         = rgen.rand_bool(); // in !dim
			dim          = rgen.rand_bool();
			shrink[dir2] = rgen.rand_uniform(0.4, 0.6)*(dir2 ? -1.0 : 1.0);
			delta_height = max(0.0f, rand_uniform(-0.1, 0.5));
		}
		else if (type == 2) { // two-part
			dim          = get_largest_xy_dim(base); // choose longest dim
			delta_height = rand_uniform(0.1, 0.5);

			for (unsigned d = 0; d < 2; ++d) {
				if (rgen.rand_bool()) {shrink[d] = rgen.rand_uniform(0.2, 0.35)*(d ? -1.0 : 1.0);}
			}
		}
		vector3d const sz(base.get_size());
		parts[0].d[ dim][ dir] += split*sz[dim]; // split in dim
		parts[1].d[ dim][!dir]  = parts[0].d[dim][dir];
		cube_t const pre_shrunk_p1(parts[1]); // save for use in details below
		for (unsigned d = 0; d < 2; ++d) {parts[1].d[!dim][d] += shrink[d]*sz[!dim];} // shrink this part in the other dim
		parts[1].z2() -= delta_height*parts[1].dz(); // lower height
		if (ADD_BUILDING_INTERIORS) {adjust_part_zvals_for_floor_spacing(parts[1]);}
		if (type == 1 && rgen.rand_bool()) {force_dim[0] = !dim; force_dim[1] = dim;} // L-shape, half the time
		else if (type == 2) {force_dim[0] = force_dim[1] = dim;} // two-part - force both parts to have roof along split dim
		int const detail_type((type == 1) ? (rgen.rand()%3) : 0); // 0=none, 1=porch, 2=detatched garage/shed
		door_dir = ((door_dim == dim) ? dir : dir2); // if we have a porch/shed/garage, put the door on that side
		if (door_dim == dim && detail_type == 0) {door_dir ^= 1;} // put it on the opposite side so that the second part isn't in the way

		if (detail_type != 0) { // add details to L-shaped house
			cube_t c(pre_shrunk_p1);
			c.d[!dim][!dir2] = parts[1].d[!dim][dir2]; // other half of the shrunk part1
			float const dist1((c.d[!dim][!dir2] - base.d[!dim][dir2])*rgen.rand_uniform(0.4, 0.6));
			float const dist2((c.d[ dim][!dir ] - base.d[ dim][dir ])*rgen.rand_uniform(0.4, 0.6));
			float const height(rgen.rand_uniform(0.55, 0.7)*parts[1].dz());

			if (gen_door) { // add door in interior of L, centered under porch roof (if it exists, otherwise where it would be)
				door_center = 0.5f*(c.d[!door_dim][0] + c.d[!door_dim][1] + ((door_dim == dim) ? dist1 : dist2));
				door_pos    = c.d[door_dim][!door_dir];
				door_part   = ((door_dim == dim) ? 0 : 1); // which part the door is connected to
				min_eq(door_height, 0.95f*height);
			}
			if (detail_type == 1) { // porch
				float const width(0.05f*(fabs(dist1) + fabs(dist2))); // width of support pillar
				c.d[!dim][dir2 ] += dist1; // move away from bcube edge
				c.d[ dim][ dir ] += dist2; // move away from bcube edge
				c.d[!dim][!dir2] -= 0.001*dist1; // adjust slightly so it's not exactly adjacent to the house and won't be considered internal face removal logic
				c.d[ dim][ !dir] -= 0.001*dist2;
				c.z1() += height; // move up
				c.z2()  = c.z1() + 0.05*parts[1].dz();
				parts.push_back(c); // porch roof
				c.z2() = c.z1();
				c.z1() = pre_shrunk_p1.z1(); // support pillar
				c.d[!dim][!dir2] = c.d[!dim][dir2] + (dir2 ? -1.0 : 1.0)*width;
				c.d[ dim][!dir ] = c.d[ dim][ dir] + (dir  ? -1.0 : 1.0)*width;
				skip_last_roof = 1;
			}
			else if (detail_type == 2) { // detatched garage/shed
				c.d[!dim][dir2 ]  = base.d[!dim][dir2]; // shove it into the opposite corner of the bcube
				c.d[ dim][dir  ]  = base.d[ dim][dir ]; // shove it into the opposite corner of the bcube
				c.d[!dim][!dir2] -= dist1; // move away from bcube edge
				c.d[ dim][!dir ] -= dist2; // move away from bcube edge
				c.z2() = c.z1() + min(min(c.dx(), c.dy()), height); // no taller than x or y size; Note: z1 same as part1
			}
			parts.push_back(c); // support column or shed/garage
		} // end house details
		calc_bcube_from_parts(); // maybe calculate a tighter bounding cube
	} // end type != 0  (multi-part house)
	else if (gen_door) { // single cube house
		door_dir  = rgen.rand_bool(); // select a random dir
		door_part = 0; // only one part
	}
	gen_interior(rgen, 0); // before adding door
	if (gen_door) {add_door(place_door(parts[door_part], door_dim, door_dir, door_height, door_center, door_pos, 0.25, 0.5, rgen), door_part, door_dim, door_dir, 0);}
	float const peak_height(rgen.rand_uniform(0.15, 0.5)); // same for all parts
	float roof_dz[3] = {0.0f};

	for (auto i = parts.begin(); (i + skip_last_roof) != parts.end(); ++i) {
		unsigned const ix(i - parts.begin()), fdim(force_dim[ix]);
		bool const dim((fdim < 2) ? fdim : get_largest_xy_dim(*i)); // use longest side if not forced
		roof_dz[ix] = gen_peaked_roof(*i, peak_height, dim);
	}
	if ((rgen.rand()%3) != 0) { // add a chimney 67% of the time
		unsigned part_ix(0);

		if (two_parts) { // start by selecting a part (if two parts)
			float const v0(parts[0].get_volume()), v1(parts[1].get_volume());
			if      (v0 > 2.0*v1) {part_ix = 0;} // choose larger part 0
			else if (v1 > 2.0*v0) {part_ix = 1;} // choose larger part 1
			else {part_ix = rgen.rand_bool();} // close in area - choose a random part
		}
		unsigned const fdim(force_dim[part_ix]);
		cube_t const &part(parts[part_ix]);
		bool const dim((fdim < 2) ? fdim : get_largest_xy_dim(part)); // use longest side if not forced
		bool dir(rgen.rand_bool());
		if (two_parts && part.d[dim][dir] != bcube.d[dim][dir]) {dir ^= 1;} // force dir to be on the edge of the house bcube (not at a point interior to the house)
		cube_t c(part);
		float const sz1(c.d[!dim][1] - c.d[!dim][0]), sz2(c.d[!dim][1] - c.d[!dim][0]);
		float shift(0.0);

		if ((rgen.rand()%3) != 0) { // make the chimney non-centered 67% of the time
			shift = sz1*rgen.rand_uniform(0.1, 0.25); // select a shift in +/- (0.1, 0.25) - no small offset from center
			if (rgen.rand_bool()) {shift = -shift;}
		}
		float const center(0.5f*(c.d[!dim][0] + c.d[!dim][1]) + shift);
		c.d[dim][!dir]  = c.d[dim][ dir] + (dir ? -0.03f : 0.03f)*(sz1 + sz2); // chimney depth
		c.d[dim][ dir] += (dir ? -0.01 : 0.01)*sz2; // slight shift from edge of house to avoid z-fighting
		c.d[!dim][0] = center - 0.05*sz1;
		c.d[!dim][1] = center + 0.05*sz1;
		c.z1()  = c.z2();
		c.z2() += rgen.rand_uniform(1.25, 1.5)*roof_dz[part_ix] - 0.4f*abs(shift);
		parts.push_back(c);
		// add top quad to cap chimney (will also update bcube to contain chimney)
		tquad_t tquad(4); // quad
		tquad.pts[0].assign(c.x1(), c.y1(), c.z2());
		tquad.pts[1].assign(c.x2(), c.y1(), c.z2());
		tquad.pts[2].assign(c.x2(), c.y2(), c.z2());
		tquad.pts[3].assign(c.x1(), c.y2(), c.z2());
		roof_tquads.emplace_back(tquad, (unsigned)tquad_with_ix_t::TYPE_CCAP); // tag as chimney cap
		has_chimney = 1;
	}
	add_roof_to_bcube();
	gen_grayscale_detail_color(rgen, 0.4, 0.8); // for roof
}

void building_t::add_door(cube_t const &c, unsigned part_ix, bool dim, bool dir, bool for_building) {

	vector3d const sz(c.get_size());
	assert(sz[dim] == 0.0 && sz[!dim] > 0.0 && sz.z > 0.0);
	tquad_with_ix_t door(4, (for_building ? (unsigned)tquad_with_ix_t::TYPE_BDOOR : (unsigned)tquad_with_ix_t::TYPE_HDOOR)); // quad
	door.pts[0].z = door.pts[1].z = c.z1(); // bottom
	door.pts[2].z = door.pts[3].z = c.z2(); // top
	door.pts[0][!dim] = door.pts[3][!dim] = c.d[!dim][ dir]; //  dir side
	door.pts[1][!dim] = door.pts[2][!dim] = c.d[!dim][!dir]; // !dir side
	door.pts[0][ dim] = door.pts[1][ dim] = door.pts[2][ dim] = door.pts[3][ dim] = c.d[dim][0] + 0.01*sz[!dim]*(dir ? 1.0 : -1.0); // move away from wall slightly
	if (dim == 0) {swap(door.pts[0], door.pts[1]); swap(door.pts[2], door.pts[3]);} // swap two corner points to flip windowing dir and invert normal for doors oriented in X
	doors.push_back(door);
	if (part_ix < 4) {door_sides[part_ix] |= 1 << (2*dim + dir);}
}

float building_t::gen_peaked_roof(cube_t const &top, float peak_height, bool dim) { // roof made from two sloped quads

	float const width(dim ? top.get_dx() : top.get_dy()), roof_dz(min(peak_height*width, top.get_dz()));
	float const z1(top.z2()), z2(z1 + roof_dz), x1(top.x1()), y1(top.y1()), x2(top.x2()), y2(top.y2());
	point pts[6] = {point(x1, y1, z1), point(x1, y2, z1), point(x2, y2, z1), point(x2, y1, z1), point(x1, y1, z2), point(x2, y2, z2)};
	if (dim == 0) {pts[4].y = pts[5].y = 0.5f*(y1 + y2);} // yc
	else          {pts[4].x = pts[5].x = 0.5f*(x1 + x2);} // xc
	unsigned const qixs[2][2][4] = {{{0,3,5,4}, {4,5,2,1}}, {{0,4,5,1}, {4,3,2,5}}}; // 2 quads
	roof_tquads.reserve(4); // 2 roof quads + 2 side triangles

	// TODO: extend outside the wall a small amount? may require updating bcube for drawing
	for (unsigned n = 0; n < 2; ++n) { // roof
		tquad_t tquad(4); // quad
		UNROLL_4X(tquad.pts[i_] = pts[qixs[dim][n][i_]];);
		roof_tquads.emplace_back(tquad, (unsigned)tquad_with_ix_t::TYPE_ROOF); // tag as roof
	}
	unsigned const tixs[2][2][3] = {{{1,0,4}, {3,2,5}}, {{0,3,4}, {2,1,5}}}; // 2 triangles

	for (unsigned n = 0; n < 2; ++n) { // triangle section/wall from z1 up to roof
		bool skip(0);

		// exclude tquads contained in/adjacent to other parts, considering only the cube parts;
		// yes, a triangle side can be occluded by a cube + another opposing triangle side from a higher wall of the house, but it's uncommon, complex, and currently ignored
		for (auto p = parts.begin(); p != parts.end(); ++p) {
			if (p->d[dim][!n] != top.d[dim][n]) continue; // not the opposing face
			if (p->z1() <= z1 && p->z2() >= z2 && p->d[!dim][0] <= top.d[!dim][0] && p->d[!dim][1] >= top.d[!dim][1]) {skip = 1; break;}
		}
		if (skip) continue;
		tquad_t tquad(3); // triangle
		UNROLL_3X(tquad.pts[i_] = pts[tixs[dim][n][i_]];);
		roof_tquads.emplace_back(tquad, (unsigned)tquad_with_ix_t::TYPE_WALL); // tag as wall
	} // for n
	return roof_dz;
}

void building_t::gen_building_doors_if_needed(rand_gen_t &rgen) {

	if (!is_cube()) return; // for now, only cube buildings can have doors; doors can be added to N-gon (non cylinder) buildings later
	assert(!parts.empty());
	float const door_height(1.1*get_door_height()), wscale(0.7); // a bit taller and a lot wider than house doors

	if (hallway_dim < 2) { // building has primary hallway, place doors at both ends of first part
		for (unsigned d = 0; d < 2; ++d) {
			add_door(place_door(parts.front(), bool(hallway_dim), d, door_height, 0.0, 0.0, 0.0, wscale, rgen), 0, bool(hallway_dim), d, 1);
		}
		return;
	}
	bool const pref_dim(rgen.rand_bool()), pref_dir(rgen.rand_bool());
	bool const has_windows(get_material().add_windows);
	bool used[4] = {0,0,0,0}; // per-side, not per-base cube
	unsigned const num_doors(1 + (rgen.rand() % (has_windows ? 3 : 4))); // 1-4; buildings with windows have at most 3 doors since they're smaller

	for (unsigned num = 0; num < num_doors; ++num) {
		bool placed(0);

		for (auto b = parts.begin(); b != parts.end() && !placed; ++b) { // try all different ground floor parts
			unsigned const part_ix(b - parts.begin());
			if (has_windows && part_ix >= 4) break; // only first 4 parts can have doors - must match first floor window removal logic
			if (b->z1() > bcube.z1()) break; // moved off the ground floor - done

			for (unsigned n = 0; n < 4; ++n) {
				bool const dim(pref_dim ^ bool(n>>1)), dir(pref_dir ^ bool(n&1));
				if (b->d[dim][dir] != bcube.d[dim][dir]) continue; // find a side on the exterior to ensure door isn't obstructed by a building cube
				if (used[2*dim + dir]) continue; // door already placed on this side
				used[2*dim + dir] = 1; // mark used
				add_door(place_door(*b, dim, dir, door_height, 0.0, 0.0, 0.1, wscale, rgen), part_ix, dim, dir, 1);
				placed = 1;
				break;
			} // for n
		} // for b
	} // for num
}

void building_t::gen_details(rand_gen_t &rgen) { // for the roof

	unsigned const num_blocks(roof_tquads.empty() ? (rgen.rand() % 9) : 0); // 0-8; 0 if there are roof quads (houses, etc.)
	has_antenna = (rgen.rand() & 1);
	details.resize(num_blocks + has_antenna);
	assert(!parts.empty());
	if (details.empty()) return; // nothing to do
	cube_t const &top(parts.back()); // top/last part

	if (num_blocks > 0) {
		float const xy_sz(top.get_size().xy_mag());
		float const height_scale(0.0035f*(top.dz() + bcube.dz())); // based on avg height of current section and entire building
		cube_t rbc(top);
		vector<point> points; // reused across calls

		for (unsigned i = 0; i < num_blocks; ++i) {
			cube_t &c(details[i]);
			float const height(height_scale*rgen.rand_uniform(1.0, 4.0));

			while (1) {
				c.set_from_point(point(rgen.rand_uniform(rbc.x1(), rbc.x2()), rgen.rand_uniform(rbc.y1(), rbc.y2()), 0.0));
				c.expand_by(vector3d(xy_sz*rgen.rand_uniform(0.01, 0.08), xy_sz*rgen.rand_uniform(0.01, 0.06), 0.0));
				if (!rbc.contains_cube_xy(c)) continue; // not contained
				if (is_simple_cube()) break; // success/done
				bool contained(1);

				for (unsigned j = 0; j < 4; ++j) { // check cylinder/ellipse
					point const pt(c.d[0][j&1], c.d[1][j>>1], 0.0); // XY only
					if (!check_part_contains_pt_xy(rbc, pt, points)) {contained = 0; break;}
				}
				if (contained) break; // success/done
			} // end while
			c.z1() = top.z2(); // z1
			c.z2() = top.z2() + height; // z2
		} // for i
	}
	if (has_antenna) { // add antenna
		float const radius(0.003f*rgen.rand_uniform(1.0, 2.0)*(top.get_dx() + top.get_dy()));
		float const height(rgen.rand_uniform(0.25, 0.5)*top.get_dz());
		cube_t &antenna(details.back());
		antenna.set_from_point(top.get_cube_center());
		antenna.expand_by(vector3d(radius, radius, 0.0));
		antenna.z1() = top.z2(); // z1
		antenna.z2() = bcube.z2() + height; // z2 (use bcube to include sloped roof)
	}
	for (auto i = details.begin(); i != details.end(); ++i) {max_eq(bcube.z2(), i->z2());} // extend bcube z2 to contain details
	if (roof_tquads.empty()) {gen_grayscale_detail_color(rgen, 0.2, 0.6);} // for antenna and roof
}

void building_t::gen_sloped_roof(rand_gen_t &rgen) { // Note: currently not supported for rotated buildings

	assert(!parts.empty());
	if (!is_simple_cube()) return; // only simple cubes are handled
	cube_t const &top(parts.back()); // top/last part
	float const peak_height(rgen.rand_uniform(0.2, 0.5));
	float const wmin(min(top.get_dx(), top.get_dy())), z1(top.z2()), z2(z1 + peak_height*wmin), x1(top.x1()), y1(top.y1()), x2(top.x2()), y2(top.y2());
	point const pts[5] = {point(x1, y1, z1), point(x1, y2, z1), point(x2, y2, z1), point(x2, y1, z1), point(0.5*(x1 + x2), 0.5*(y1 + y2), z2)};
	float const d1(rgen.rand_uniform(0.0, 0.8));

	if (d1 < 0.2) { // pointed roof with 4 sloped triangles
		unsigned const ixs[4][3] = {{1,0,4}, {3,2,4}, {0,3,4}, {2,1,4}};
		roof_tquads.resize(4);

		for (unsigned n = 0; n < 4; ++n) {
			roof_tquads[n].npts = 3; // triangles
			UNROLL_3X(roof_tquads[n].pts[i_] = pts[ixs[n][i_]];)
		}
	}
	else { // flat roof with center quad and 4 surrounding sloped quads
		point const center((1.0 - d1)*pts[4]);
		point pts2[8];
		for (unsigned n = 0; n < 4; ++n) {pts2[n] = pts[n]; pts2[n+4] = d1*pts[n] + center;}
		unsigned const ixs[5][4] = {{4,7,6,5}, {0,4,5,1}, {3,2,6,7}, {0,3,7,4}, {2,1,5,6}}; // add the flat quad first, which works better for sphere intersections
		roof_tquads.resize(5);

		for (unsigned n = 0; n < 5; ++n) {
			roof_tquads[n].npts = 4; // quads
			UNROLL_4X(roof_tquads[n].pts[i_] = pts2[ixs[n][i_]];)
		}
	}
	add_roof_to_bcube();
	//max_eq(bcube.z2(), z2);
	gen_grayscale_detail_color(rgen, 0.4, 0.8); // for antenna and roof
}

void building_t::add_roof_to_bcube() {
	for (auto i = roof_tquads.begin(); i != roof_tquads.end(); ++i) {i->update_bcube(bcube);} // technically should only need to update z2
}
void building_t::gen_grayscale_detail_color(rand_gen_t &rgen, float imin, float imax) {
	float const cscale(rgen.rand_uniform(imin, imax));
	detail_color = colorRGBA(cscale, cscale, cscale, 1.0);
}


// *** Interiors ***

void remove_section_from_cube(cube_t &c, cube_t &c2, float v1, float v2, bool xy) { // c is input+output cube, c2 is other output cube
	//if (!(v1 > c.d[xy][0] && v1 < v2 && v2 < c.d[xy][1])) {cout << TXT(v1) << TXT(v2) << TXT(c.d[xy][0]) << TXT(c.d[xy][1]) << TXT(xy) << endl;}
	assert(v1 > c.d[xy][0] && v1 < v2 && v2 < c.d[xy][1]); // v1/v2 must be interior values for cube
	c2 = c; // clone first cube
	c.d[xy][1] = v1; c2.d[xy][0] = v2; // c=low side, c2=high side
}
float cube_rand_side_pos(cube_t const &c, int dim, float min_dist_param, float min_dist_abs, rand_gen_t &rgen) {
	assert(dim < 3);
	assert(min_dist_param < 0.5f); // aplies to both ends
	float const lo(c.d[dim][0]), hi(c.d[dim][1]), delta(hi - lo), gap(max(min_dist_abs, min_dist_param*delta));
	//if ((hi-gap) <= (lo+gap)) {cout << TXT(dim) << TXT(lo) << TXT(hi) << TXT(min_dist_abs) << TXT(delta) << TXT(gap) << endl;}
	return rgen.rand_uniform((lo + gap), (hi - gap));
}

// see global_building_params.window_xspace/window_width
int building_t::get_num_windows_on_side(float xy1, float xy2) const {
	assert(xy1 < xy2);
	building_mat_t const &mat(get_material());
	float tscale(2.0f*mat.get_window_tx()), t0(tscale*xy1), t1(tscale*xy2);
	clip_low_high(t0, t1);
	return round_fp(t1 - t0);
}

// Note: wall should start out equal to the room bcube
void create_wall(cube_t &wall, bool dim, float wall_pos, float fc_thick, float wall_half_thick, float wall_edge_spacing) {
	wall.z1() += fc_thick; // start at the floor
	wall.z2() -= fc_thick; // start at the ceiling
	wall.d[ dim][0] = wall_pos - wall_half_thick;
	wall.d[ dim][1] = wall_pos + wall_half_thick;
	// move a bit away from the exterior wall to prevent z-fighting; we might want to add walls around the building exterior and cut window holes
	wall.d[!dim][0] += wall_edge_spacing;
	wall.d[!dim][1] -= wall_edge_spacing;
}

// Note: assumes edge is not clipped and doesn't work when clipped
bool is_val_inside_window(cube_t const &c, bool dim, float val, float window_spacing, float window_border) {
	float const uv(fract((val - c.d[dim][0])/window_spacing));
	return (uv > window_border && uv < 1.0f-window_border);
}

struct split_cube_t : public cube_t {
	float door_lo[2][2], door_hi[2][2]; // per {dim x dir}
	
	split_cube_t(cube_t const &c) : cube_t(c) {
		door_lo[0][0] = door_lo[0][1] = door_lo[1][0] = door_lo[1][1] = door_hi[0][0] = door_hi[0][1] = door_hi[1][0] = door_hi[1][1] = 0.0f;
	}
	bool bad_pos(float val, bool dim) const {
		for (unsigned d = 0; d < 2; ++d) { // check both dirs (wall end points)
			if (door_lo[dim][d] < door_hi[dim][d] && val > door_lo[dim][d] && val < door_hi[dim][d]) return 1;
		}
		return 0;
	}
};

unsigned calc_num_floors(cube_t const &c, float window_vspacing, float floor_thickness) {
	float const z_span(c.dz() - floor_thickness);
	assert(z_span > 0.0);
	unsigned const num_floors(round_fp(z_span/window_vspacing)); // round - no partial floors
	assert(num_floors <= 100); // sanity check
	return num_floors;
}

void building_t::gen_interior(rand_gen_t &rgen, bool has_overlapping_cubes) { // Note: contained in building bcube, so no bcube update is needed

	if (!ADD_BUILDING_INTERIORS) return; // disabled
	if (world_mode != WMODE_INF_TERRAIN) return; // tiled terrain mode only
	if (!global_building_params.windows_enabled()) return; // no windows, can't assign floors and generate interior
	//if (has_overlapping_cubes) return; // overlapping cubes buildings are more difficult to handle
	if (!is_cube()) return; // only generate interiors for cube buildings for now
	building_mat_t const &mat(get_material());
	if (!mat.add_windows) return; // not a building type that has generated windows (skip office buildings with windows baked into textures)
	// defer this until the building is close to the player?
	interior.reset(new building_interior_t);
	float const window_vspacing(mat.get_floor_spacing());
	float const floor_thickness(0.1*window_vspacing), fc_thick(0.5*floor_thickness);
	float const doorway_width(0.5*window_vspacing), doorway_hwidth(0.5*doorway_width);
	float const wall_thick(0.5*floor_thickness), wall_half_thick(0.5*wall_thick), wall_edge_spacing(0.05*wall_thick), min_wall_len(4.0*doorway_width);
	float const wwf(global_building_params.get_window_width_fract()), window_border(0.5*(1.0 - wwf)); // (0.0, 1.0)
	unsigned wall_seps_placed[2][2] = {0}; // bit masks for which wall separators have been placed per part, one per {dim x dir}; scales to 32 parts, which should be enough
	vector<split_cube_t> to_split;
	bool has_hallway_with_rooms(0);
	
	// generate walls and floors for each part;
	// this will need to be modified to handle buildings that have overlapping parts, or skip those building types completely
	for (auto p = parts.begin(); p != (parts.end() - has_chimney); ++p) {
		if (is_house && (p - parts.begin()) > 1) break; // houses have at most two parts; exclude garage, shed, porch, porch support, etc.
		unsigned const num_floors(calc_num_floors(*p, window_vspacing, floor_thickness));
		if (num_floors == 0) continue; // not enough space to add a floor (can this happen?)
		// for now, assume each part has the same XY bounds and can use the same floorplan; this means walls can span all floors and don't need to be duplicated for each floor
		vector3d const psz(p->get_size());
		bool const min_dim(psz.y < psz.x); // hall dim
		float const cube_width(psz[min_dim]);

		if (!is_house && (p+1 == parts.end() || (p+1)->z1() > p->z1()) && cube_width > 4.0*min_wall_len) {
			// building with rectangular slice (no adjacent exterior walls at this level), generate rows of offices
			has_hallway_with_rooms = 1;
			int const num_windows   (get_num_windows_on_side(p->d[!min_dim][0], p->d[!min_dim][1]));
			int const num_windows_od(get_num_windows_on_side(p->d[ min_dim][0], p->d[ min_dim][1])); // other dim, for use in hallway width calculation
			int const windows_per_room((num_windows > 5) ? 2 : 1); // 1-2 windows per room
			int const num_rooms((num_windows+windows_per_room-1)/windows_per_room); // round up
			bool const partial_room((num_windows % windows_per_room) != 0); // an odd number of windows leaves a small room at the end
			assert(num_rooms >= 0 && num_rooms < 1000); // sanity check
			float const window_hspacing(psz[!min_dim]/num_windows), room_len(window_hspacing*windows_per_room);
			float const hall_width(((num_windows_od & 1) ? 1 : 2)*psz[min_dim]/num_windows_od); // hall either contains 1 (odd) or 2 (even) windows
			float const room_width(0.5f*(cube_width - hall_width)); // rooms are the same size on each side of the hallway
			float const hwall_extend(0.5f*(room_len - doorway_width - wall_thick));
			float const hall_wall_pos[2] = {(p->d[min_dim][0] + room_width), (p->d[min_dim][1] - room_width)};
			hallway_dim = !min_dim; // cache in building for later use
			vect_cube_t &room_walls(interior->walls[!min_dim]), &hall_walls(interior->walls[min_dim]);
			cube_t rwall(*p); // copy from part; shared zvals, but X/Y will be overwritten per wall
			float const wall_pos(p->d[!min_dim][0] + room_len); // pos of first wall separating first from second rooms
			create_wall(rwall, !min_dim, wall_pos, fc_thick, wall_half_thick, wall_edge_spacing); // room walls

			for (int i = 0; i+1 < num_rooms; ++i) { // num_rooms-1 walls
				for (unsigned d = 0; d < 2; ++d) {
					room_walls.push_back(rwall);
					room_walls.back().d[min_dim][!d] = hall_wall_pos[d];
					cube_t hwall(room_walls.back());
					for (unsigned e = 0; e < 2; ++e) {hwall.d[ min_dim][e]  = hall_wall_pos[d] + (e ? 1.0f : -1.0f)*wall_half_thick;}
					for (unsigned e = 0; e < 2; ++e) {hwall.d[!min_dim][e] += (e ? 1.0f : -1.0f)*hwall_extend;}
					if (partial_room && i+2 == num_rooms) {hwall.d[!min_dim][1] -= 1.5*doorway_width;} // pull back a bit to make room for a doorway at the end of the hall
					hall_walls.push_back(hwall); // longer sections that form T-junctions with room walls
				}
				for (unsigned e = 0; e < 2; ++e) {rwall.d[!min_dim][e] += room_len;}
			} // for i
			for (unsigned s = 0; s < 2; ++s) { // add half length hall walls at each end of the hallway
				cube_t hwall(rwall); // copy to get correct zvals
				float const hwall_len((partial_room && s == 1) ? doorway_width : hwall_extend); // hwall for partial room at end is only length doorway_width
				hwall.d[!min_dim][ s] = p->d   [!min_dim][s] + (s ? -1.0f : 1.0f)*wall_edge_spacing; // end at the wall
				hwall.d[!min_dim][!s] = hwall.d[!min_dim][s] + (s ? -1.0f : 1.0f)*hwall_len; // end at first doorway

				for (unsigned d = 0; d < 2; ++d) {
					for (unsigned e = 0; e < 2; ++e) {hwall.d[ min_dim][e] = hall_wall_pos[d] + (e ? 1.0f : -1.0f)*wall_half_thick;}
					hall_walls.push_back(hwall);
				}
			} // for s
			// add rooms
			bool const add_hall(0); // I guess the hall itself doesn't count as a room
			interior->rooms.reserve(2*num_rooms + add_hall); // two rows of rooms + optional hallway
			float pos(p->d[!min_dim][0]);

			for (int i = 0; i < num_rooms; ++i) {
				float const next_pos(min(p->d[!min_dim][1], (pos + room_len))); // clamp to end of building to last row handle partial room)

				for (unsigned d = 0; d < 2; ++d) { // lo, hi
					cube_t c(*p); // copy zvals and exterior wall pos
					c.d[ min_dim][!d] = hall_wall_pos[d];
					c.d[!min_dim][ 0] = pos;
					c.d[!min_dim][ 1] = next_pos;
					interior->rooms.push_back(c);
				}
				pos = next_pos;
			} // for i
			if (add_hall) {
				cube_t hall(*p);
				for (unsigned e = 0; e < 2; ++e) {hall.d[min_dim][e] = hall_wall_pos[e];}
				interior->rooms.push_back(hall);
			}
		}
		else { // generate random walls using recursive 2D slices
			unsigned const part_mask(1 << (p - parts.begin()));
			assert(to_split.empty());
			to_split.emplace_back(*p); // seed room is entire part, no door
			float window_hspacing[2] = {0.0};
			
			for (unsigned d = 0; d < 2; ++d) {
				int const num_windows(get_num_windows_on_side(p->d[d][0], p->d[d][1]));
				window_hspacing[d] = psz[d]/num_windows;
				interior->walls[d].reserve(parts.size()); // likely at least this many
			}
			while (!to_split.empty()) {
				split_cube_t c(to_split.back()); // Note: non-const because door_lo/door_hi is modified during T-junction insert
				to_split.pop_back();
				vector3d const csz(c.get_size());
				bool wall_dim(0); // which dim the room is split by
				if      (csz.y > min_wall_len && csz.x > 1.25*csz.y) {wall_dim = 0;} // split long room in x
				else if (csz.x > min_wall_len && csz.y > 1.25*csz.x) {wall_dim = 1;} // split long room in y
				else {wall_dim = rgen.rand_bool();} // choose a random split dim for nearly square rooms
				if (min(csz.x, csz.y) < min_wall_len) continue; // not enough space to add a wall (chimney, porch support, garage, shed, etc.)
				float wall_pos(0.0);
				bool const on_edge(c.d[wall_dim][0] == p->d[wall_dim][0] || c.d[wall_dim][1] == p->d[wall_dim][1]); // at edge of the building - make sure walls don't intersect windows
				bool pos_valid(0);
				
				for (unsigned num = 0; num < 20; ++num) { // 20 tries to choose a wall pos that's not inside a window
					wall_pos = cube_rand_side_pos(c, wall_dim, 0.25, (doorway_width + wall_thick), rgen);
					if (on_edge && is_val_inside_window(*p, wall_dim, wall_pos, window_hspacing[wall_dim], window_border)) continue; // try a new wall_pos
					if (c.bad_pos(wall_pos, wall_dim)) continue; // intersects doorway from prev wall, try a new wall_pos
					pos_valid = 1; break; // done, keep wall_pos
				}
				if (!pos_valid) { // no valid pos, skip this split
					interior->rooms.push_back(c);
					continue;
				}
				cube_t wall(c), wall2, wall3; // copy from cube; shared zvals, but X/Y will be overwritten per wall
				create_wall(wall, wall_dim, wall_pos, fc_thick, wall_half_thick, wall_edge_spacing);

				// determine if either end of the wall ends at an adjacent part and insert an extra wall there to form a T junction
				for (auto p2 = parts.begin(); p2 != parts.end(); ++p2) {
					unsigned const part_mask2(1 << (p2 - parts.begin()));

					for (unsigned dir = 0; dir < 2; ++dir) {
						float const val(c.d[!wall_dim][dir]);
						if (p2 == p) continue; // skip self
						if (p2->d[!wall_dim][!dir] != val) continue; // not adjacent
						if (p2->z1() >= c.z2() || p2->z2() <= c.z1()) continue; // no overlap in Z
						if (p2->d[wall_dim][0] >= wall_pos || p2->d[wall_dim][1] <= wall_pos) continue; // no overlap in wall_dim
						if (wall_seps_placed[wall_dim][!dir] & part_mask2) continue; // already placed a separator for this part, don't add a duplicate
						wall3.z1() = max(c.z1(), p2->z1()) + fc_thick; // shared Z range
						wall3.z2() = min(c.z2(), p2->z2()) - fc_thick;
						wall3.d[ wall_dim][0] = max(c.d[wall_dim][0], p2->d[wall_dim][0]) + wall_edge_spacing; // shared wall_dim range with slight offset
						wall3.d[ wall_dim][1] = min(c.d[wall_dim][1], p2->d[wall_dim][1]) - wall_edge_spacing;
						wall3.d[!wall_dim][ dir] = val;
						wall3.d[!wall_dim][!dir] = val + (dir ? -1.0 : 1.0)*wall_thick;

						for (unsigned s = 0; s < 2; ++s) { // add doorways to both sides of wall_pos if there's space, starting with the high side
							if (fabs(wall3.d[wall_dim][!s] - wall_pos) > 1.5f*doorway_width) {
								float const doorway_pos(0.5f*(wall_pos + wall3.d[wall_dim][!s])); // centered, for now
								float const lo_pos(doorway_pos - doorway_hwidth), hi_pos(doorway_pos + doorway_hwidth);
								remove_section_from_cube(wall3, wall2, lo_pos, hi_pos, wall_dim);
								interior->walls[!wall_dim].push_back(wall2);
								// TODO_INT: this doesn't work, need to set this on the other part as well, but the walls may already have been generated there
								c.door_lo[wall_dim][dir] = lo_pos - wall_half_thick; // set new door pos in this dim
								c.door_hi[wall_dim][dir] = hi_pos + wall_half_thick;
							}
						} // for s
						interior->walls[!wall_dim].push_back(wall3);
						wall_seps_placed[wall_dim][ dir] |= part_mask;  // mark this wall as placed
						wall_seps_placed[wall_dim][!dir] |= part_mask2; // mark this wall as placed for other part
					} // for dir
				} // for p2
				float const doorway_pos(cube_rand_side_pos(c, !wall_dim, 0.25, doorway_width, rgen));
				float const lo_pos(doorway_pos - doorway_hwidth), hi_pos(doorway_pos + doorway_hwidth);
				remove_section_from_cube(wall, wall2, lo_pos, hi_pos, !wall_dim);
				interior->walls[wall_dim].push_back(wall);
				interior->walls[wall_dim].push_back(wall2);

				if (csz[wall_dim] > max(global_building_params.wall_split_thresh, 1.0f)*min_wall_len) { // split into two smaller rooms
					for (unsigned d = 0; d < 2; ++d) { // still have space to split in other dim, add the two parts to the stack
						split_cube_t c_sub(c);
						c_sub.d[wall_dim][d] = wall.d[wall_dim][!d]; // clip to wall pos
						c_sub.door_lo[!wall_dim][d] = lo_pos - wall_half_thick; // set new door pos in this dim (keep door pos in other dim, if set)
						c_sub.door_hi[!wall_dim][d] = hi_pos + wall_half_thick;
						to_split.push_back(c_sub);
					}
				}
				else {interior->rooms.push_back(c);} // leaf case (unsplit), add a new room
			} // end while()
		} // end wall placement
		// add ceilings and floors; we have num_floors+1 separators; the first is only a floor, and the last is only a ceiling
		interior->ceilings.reserve(num_floors);
		interior->floors  .reserve(num_floors);
		float z(p->z1());

		for (unsigned f = 0; f <= num_floors; ++f, z += window_vspacing) {
			cube_t c(*p);
			if (f > 0         ) {c.z1() = z - fc_thick; c.z2() = z; interior->ceilings.push_back(c);}
			if (f < num_floors) {c.z1() = z; c.z2() = z + fc_thick; interior->floors  .push_back(c);}
			c.z1() = z + fc_thick; c.z2() = z + window_vspacing - fc_thick;
			// add per-floor walls, door cutouts, etc. here if needed
		} // for f
	} // for p
	if (!has_hallway_with_rooms) { // random slicing plane rooms
		// attempt to cut extra doorways into long walls if there's space to produce a more connected floorplan
		float const min_split_len(1.5*min_wall_len);

		for (unsigned d = 0; d < 2; ++d) { // x,y: dim in which the wall partitions the room (wall runs in dim !d)
			vect_cube_t &walls(interior->walls[d]);
			vect_cube_t const &perp_walls(interior->walls[!d]);

			for (unsigned w = 0; w < walls.size(); ++w) { // Note: iteration will include newly added all segments to recursively split long walls
				for (unsigned nsplits = 0; nsplits < 4; ++nsplits) { // at most 4 splits
					cube_t &wall(walls[w]); // take a reference here because a prev iteration push_back() may have invalidated it
					float const len(wall.d[!d][1] - wall.d[!d][0]);
					if (len < min_split_len) break; // not long enough to split - done
					// walls currently don't run along the inside of exterior building walls, so we don't need to handle that case yet
					bool was_split(0);

					for (unsigned ntries = 0; ntries < 4; ++ntries) { // 4 tries: choose random doorway positions and check against perp_walls for occlusion
						float const doorway_pos(cube_rand_side_pos(wall, !d, 0.25, doorway_width, rgen));
						float const lo_pos(doorway_pos - doorway_hwidth), hi_pos(doorway_pos + doorway_hwidth);
						bool valid(1);

						for (auto p = perp_walls.begin(); p != perp_walls.end(); ++p) {
							if (p->d[!d][1] < lo_pos-wall_thick || p->d[!d][0] > hi_pos+wall_thick) continue; // no overlap with wall
							if (p->d[ d][1] > wall.d[d][0]-wall_thick && p->d[ d][0] < wall.d[d][1]+wall_thick) {valid = 0; break;} // has perp intersection
						}
						if (!valid) continue;
						cube_t wall2;
						remove_section_from_cube(wall, wall2, lo_pos, hi_pos, !d);
						walls.push_back(wall2); // Note: invalidates wall reference
						was_split = 1;
						break;
					} // for ntries
					if (!was_split) break; // no more splits
				} // for nsplits
			} // for w
		} // for d
	}
	gen_room_details(rgen, wall_thick, floor_thickness, window_vspacing);
}

// Note: these three floats can be calculated from mat.get_floor_spacing(), but it's easier to change the constants if we just pass them in
void building_t::gen_room_details(rand_gen_t &rgen, float wall_spacing, float floor_thickness, float window_vspacing) {

	return; // TODO_INT: enable when this code is complete enough to do something useful
	assert(interior);
	if (interior->room_geom) return; // already generated?
	interior->room_geom.reset(new building_room_geom_t);
	vector<colored_cube_t> &cubes(interior->room_geom->cubes);
	float const fc_thick(0.5*floor_thickness);

	for (auto r = interior->rooms.begin(); r != interior->rooms.end(); ++r) {
		unsigned const num_floors(calc_num_floors(*r, window_vspacing, floor_thickness));
		point room_center(r->get_cube_center());
		float z(r->z1());

		for (unsigned f = 0; f <= num_floors; ++f, z += window_vspacing) {
			// TODO_INT: generate objects for this room+floor combination
			room_center.z = z + fc_thick; // floor height
			vector3d table_sz;
			for (unsigned d = 0; d < 3; ++d) {table_sz[d] = 2.0*wall_spacing*(1.0 + rgen.rand_float());}
			point llc(room_center - table_sz), urc(room_center + table_sz);
			llc.z = room_center.z; // bottom is not shifted below the floor
			cube_t table(llc, urc);
			cubes.emplace_back(table, BROWN, 16); // skip_faces=16/Z1
			//if (f == 0 && r->z1() == bcube.z1()) {} // any special logic that goes on the first floor is here
		}
	} // for r
	interior->room_geom->create_vbo(); // I guess we always do this here? why create the geometry if we're not going to draw it
}

void building_t::update_stats(building_stats_t &s) const { // calculate all of the counts that are easy to get

	++s.nbuildings;
	s.nparts   += parts.size();
	s.ndetails += details.size();
	s.ntquads  += roof_tquads.size();
	s.ndoors   += doors.size();
	if (!interior) return;
	++s.ninterior;
	s.nrooms  += interior->rooms.size();
	s.nceils  += interior->ceilings.size();
	s.nfloors += interior->floors.size();
	s.nwalls  += interior->walls[0].size() + interior->walls[1].size();
	if (!interior->room_geom) return;
	++s.nrgeom;
	s.ngeom  += interior->room_geom->cubes.size();
	s.nverts += interior->room_geom->num_verts;
}

