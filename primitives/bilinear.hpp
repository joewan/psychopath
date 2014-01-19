#ifndef BILINEAR_HPP
#define BILINEAR_HPP

#include "numtype.h"

#include <atomic>
#include <vector>
#include "vector.hpp"
#include "grid.hpp"
#include "primitive.hpp"
#include "timebox.hpp"

/*
 * A bilinear patch.
 * Vertices arranged like this:
 *     u-->
 *   v1----v2
 * v  |    |
 * | v4----v3
 * \/
 */
class Bilinear: public DiceableSurfacePrimitive
{
public:
	TimeBox<Vec3 *> verts;
	float u_min, v_min, u_max, v_max;


	BBoxT bbox;
	bool has_bounds;

	Bilinear(uint16_t res_time_);
	Bilinear(Vec3 v1, Vec3 v2, Vec3 v3, Vec3 v4);
	virtual ~Bilinear();

	void add_time_sample(int samp, Vec3 v1, Vec3 v2, Vec3 v3, Vec3 v4);
	Grid *grid_dice(const int ru, const int rv);

	virtual BBoxT &bounds();

	virtual void split(std::unique_ptr<DiceableSurfacePrimitive> primitives[]);
	virtual size_t subdiv_estimate(float width) const;
	virtual std::shared_ptr<MicroSurface> dice(size_t subdivisions);
};

#endif
