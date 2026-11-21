/** @file */

#pragma once

#include "tinyspline.h"
#include <vector>
#include <string>

#ifndef TINYSPLINECXX_API
#define TINYSPLINECXX_API TINYSPLINE_API
#endif



namespace tinyspline {

/*! @name API Configuration
 *
 * @{
 */
typedef tsReal real;
/*! @} */



/*! @name Swig Type Mapping
 *
 * Methods that do not return or set the value of a class attribute (let's call
 * these methods `non-accessor methods') must return/take instances of
 * std::vector as pointer. Otherwise, they aren't type mapped by Swig to the
 * std::vector representation of the target language.
 *
 * @{
 */
#ifdef SWIG
using std_real_vector_in = std::vector<tinyspline::real> *;
using std_real_vector_out = std::vector<tinyspline::real> *;
#else
using std_real_vector_in = const std::vector<tinyspline::real> &;
using std_real_vector_out = std::vector<tinyspline::real>;
#endif
/*! @} */



/*! @name Vector Math
 *
 * Wrapper classes for TinySpline's vector math.
 *
 * @{
 */
class TINYSPLINECXX_API Vec2 {
public:
	Vec2();
	Vec2(real x, real y);

	Vec2 operator+(const Vec2 &other);
	Vec2 operator-(const Vec2 &other);
	Vec2 operator*(real scalar);

	real x() const;
	void setX(real val);
	real y() const;
	void setY(real val);
	std::vector<real> values() const;

	Vec2 add(const Vec2 &other) const;
	Vec2 subtract(const Vec2 &other) const;
	Vec2 multiply(real scalar) const;
	Vec2 normalize() const;
	real magnitude() const;
	real dot(const Vec2 &other) const;
	real angle(const Vec2 &other) const;
	real distance(const Vec2 &other) const;

	std::string toString() const;

private:
	real m_vals[2];
};

class TINYSPLINECXX_API Vec3 {
public:
	Vec3();
	Vec3(real x, real y, real z);

	Vec3 operator+(const Vec3 &other);
	Vec3 operator-(const Vec3 &other);
	Vec3 operator*(real scalar);

	real x() const;
	void setX(real val);
	real y() const;
	void setY(real val);
	real z() const;
	void setZ(real val);
	std::vector<real> values() const;

	Vec3 add(const Vec3 &other) const;
	Vec3 subtract(const Vec3 &other) const;
	Vec3 multiply(real scalar) const;
	Vec3 cross(const Vec3 &other) const;
	Vec3 normalize() const;
	real magnitude() const;
	real dot(const Vec3 &other) const;
	real angle(const Vec3 &other) const;
	real distance(const Vec3 &other) const;

	std::string toString() const;

private:
	real m_vals[3];
};

class TINYSPLINECXX_API Vec4 {
public:
	Vec4();
	Vec4(real x, real y, real z, real w);

	Vec4 operator+(const Vec4 &other);
	Vec4 operator-(const Vec4 &other);
	Vec4 operator*(real scalar);

	real x() const;
	void setX(real val);
	real y() const;
	void setY(real val);
	real z() const;
	void setZ(real val);
	real w() const;
	void setW(real val);
	std::vector<real> values() const;

	Vec4 add(const Vec4 &other) const;
	Vec4 subtract(const Vec4 &other) const;
	Vec4 multiply(real scalar) const;
	Vec4 normalize() const;
	real magnitude() const;
	real dot(const Vec4 &other) const;
	real angle(const Vec4 &other) const;
	real distance(const Vec4 &other) const;

	std::string toString() const;

private:
	real m_vals[4];
};
/*! @} */



/*! @name Spline Framing
 *
 * Wrapper classes for ::tsFrame (::Frame) and sequences of ::tsFrame
 * (::FrameSeq). Instances of ::FrameSeq are created by ::BSpline::computeRMF.
 *
 * @{
 */
class TINYSPLINECXX_API Frame {
public:
	Frame(Vec3 &position,
	      Vec3 &tangent,
	      Vec3 &normal,
	      Vec3 &binormal);

	Vec3 position() const;
	Vec3 tangent() const;
	Vec3 normal() const;
	Vec3 binormal() const;

	std::string toString() const;

private:
	Vec3 m_position,
	     m_tangent,
	     m_normal,
	     m_binormal;
};

class TINYSPLINECXX_API FrameSeq {
public:
	FrameSeq();
	FrameSeq(const FrameSeq &other);
	FrameSeq(FrameSeq &&other);
	virtual ~FrameSeq();

	FrameSeq &operator=(const FrameSeq &other);
	FrameSeq &operator=(FrameSeq &&other);

	size_t size() const;
	Frame at(size_t idx) const;

	std::string toString() const;

private:
	tsFrame *m_frames;
	size_t m_size;
	FrameSeq(tsFrame *frames,
	         size_t len);
	friend class BSpline;
};
/*! @} */



/*! @name Utility Classes
 *
 * Little helper classes, such as value classes or classes with only static
 * methods. These are primarily classes that do not really fit into another
 * group or are too small to form their own group.
 *
 * @{
 */
class TINYSPLINECXX_API Domain {
public:
	Domain(real min, real max);

	real min() const;
	real max() const;

	std::string toString() const;

private:
	real m_min,
	     m_max;
};
/*! @} */



/*! @name DeBoorNet
 *
 * Wrapper class for ::tsDeBoorNet.
 *
 * @{
 */
class TINYSPLINECXX_API DeBoorNet {
public:
	DeBoorNet(const DeBoorNet &other);
	DeBoorNet(DeBoorNet &&other);
	virtual ~DeBoorNet();

	DeBoorNet & operator=(const DeBoorNet &other);
	DeBoorNet & operator=(DeBoorNet &&other);

	real knot() const;
	size_t index() const;
	size_t multiplicity() const;
	size_t numInsertions() const;
	size_t dimension() const;
	std::vector<real> points() const;
	std::vector<real> result() const;

	/**
	* Returns the result at \p idx as ::Vec2. Note that, by design, \p idx
	* cannot be greater than \c 1. It is safe to call this method even if
	* ::dimension is less than \c 2. In this case, the missing components
	* are set to \c 0. If ::dimension is greater than \c 2, the excess
	* values are ignored.
	*
	* @return
	* 	The result at \p idx as ::Vec2.
	* @throws std::out_of_range
	* 	If \p idx is greater than \c 1, or if \p idx is \c 1, but there
	* 	is only one result.
	*/
	Vec2 resultVec2(size_t idx = 0) const;

	/**
	* Returns the result at \p idx as ::Vec3. Note that, by design, \p idx
	* cannot be greater than \c 1. It is safe to call this method even if
	* ::dimension is less than \c 3. In this case, the missing components
	* are set to \c 0. If ::dimension is greater than \c 3, the excess
	* values are ignored.
	*
	* @return
	* 	The result at \p idx as ::Vec3.
	* @throws std::out_of_range
	* 	If \p idx is greater than \c 1, or if \p idx is \c 1, but there
	* 	is only one result.
	*/
	Vec3 resultVec3(size_t idx = 0) const;

	/**
	* Returns the result at \p idx as ::Vec4. Note that, by design, \p idx
	* cannot be greater than \c 1. It is safe to call this method even if
	* ::dimension is less than \c 4. In this case, the missing components
	* are set to \c 0. If ::dimension is greater than \c 4, the excess
	* values are ignored.
	*
	* @return
	* 	The result at \p idx as ::Vec4.
	* @throws std::out_of_range
	* 	If \p idx is greater than \c 1, or if \p idx is \c 1, but there
	* 	is only one result.
	*/
	Vec4 resultVec4(size_t idx = 0) const;

	std::string toString() const;

private:
	tsDeBoorNet m_net;

	/* Constructors & Destructors */
	explicit DeBoorNet(tsDeBoorNet &data);

	friend class BSpline;
};
/*! @} */



/*! @name BSpline
 *
 * Wrapper class for ::tsBSpline.
 *
 * @{
 */
class Morphism;
class ChordLengths;
class TINYSPLINECXX_API BSpline {
public:
	enum Type { Opened, Clamped, Beziers };

	/* Constructors & Destructors */
	BSpline();
	BSpline(const BSpline &other);
	BSpline(BSpline &&other);
	explicit BSpline(size_t numControlPoints,
	                 size_t dimension = 2,
	                 size_t degree = 3,
	                 Type type = Type::Clamped);
	virtual ~BSpline();

	/* Create from static method */
	static BSpline interpolateCubicNatural(std_real_vector_in points,
	                                       size_t dimension);
	static BSpline interpolateCatmullRom(std_real_vector_in points,
	                                     size_t dimension,
	                                     real alpha = (real) 0.5,
	                                     std::vector<real> *first = nullptr,
	                                     std::vector<real> *last = nullptr,
	                                     real epsilon = TS_POINT_EPSILON);
	static BSpline parseJson(std::string json);
	static BSpline load(std::string path);

	static bool knotsEqual(real x, real y);

	/* Operators */
	BSpline & operator=(const BSpline &other);
	BSpline & operator=(BSpline &&other);
	DeBoorNet operator()(real knot) const;

	/* Accessors */
	size_t degree() const;
	size_t order() const;
	size_t dimension() const;
	std::vector<real> controlPoints() const;
	Vec2 controlPointVec2At(size_t idx) const;
	Vec3 controlPointVec3At(size_t idx) const;
	Vec4 controlPointVec4At(size_t idx) const;
	std::vector<real> knots() const;
	real knotAt(size_t idx) const;

	/* Query */
	size_t numControlPoints() const;
	DeBoorNet eval(real knot) const;
	std_real_vector_out evalAll(std_real_vector_in knots) const;
	std_real_vector_out sample(size_t num = 0) const;
	DeBoorNet bisect(real value,
	                 real epsilon = (real) 0.0,
	                 bool persnickety = false,
	                 size_t index = 0,
	                 bool ascending = true,
	                 size_t maxIter = 50) const;
	Domain domain() const;
	bool isClosed(real epsilon = TS_POINT_EPSILON) const;
	FrameSeq computeRMF(std_real_vector_in knots,
	                    Vec3 *firstNormal = nullptr) const;
	BSpline subSpline(real knot0, real knot1) const;
	std_real_vector_out uniformKnotSeq(size_t num = 100) const;
	std_real_vector_out equidistantKnotSeq(size_t num = 100,
	                                       size_t numSamples = 0) const;
	ChordLengths chordLengths(std_real_vector_in knots) const;
	ChordLengths chordLengths(size_t numSamples = 200) const;

	/* Serialization */
	std::string toJson() const;
	void save(std::string path) const;

	/* Modifications */
	void setControlPoints(const std::vector<real> &ctrlp);
	void setControlPointVec2At(size_t idx, Vec2 &cp);
	void setControlPointVec3At(size_t idx, Vec3 &cp);
	void setControlPointVec4At(size_t idx, Vec4 &cp);
	void setKnots(const std::vector<real> &knots);
	void setKnotAt(size_t idx, real knot);

	/* Transformations */
	BSpline insertKnot(real knot, size_t num) const;
	BSpline split(real knot) const;
	BSpline tension(real beta) const;
	BSpline toBeziers() const;
	BSpline derive(size_t num = 1,
	               real eps = TS_POINT_EPSILON) const;
	BSpline elevateDegree(size_t amount,
	                      real eps = TS_POINT_EPSILON) const;
	BSpline alignWith(const BSpline &other,
	                  BSpline &otherAligned,
	                  real eps = TS_POINT_EPSILON) const;
	Morphism morphTo(const BSpline &other,
	                 real eps = TS_POINT_EPSILON) const;

	/* Debug */
	std::string toString() const;

private:
	tsBSpline m_spline;

	/* Constructors & Destructors */
	explicit BSpline(tsBSpline &data);

	/* Needs to access ::spline. */
	friend class Morphism;
};
/*! @} */



/*! @name Spline Morphing
 *
 * @{
 */
class TINYSPLINECXX_API Morphism {
public:
	Morphism(const BSpline &origin,
		 const BSpline &target,
		 real epsilon = TS_POINT_EPSILON);

	BSpline origin() const;
	BSpline target() const;
	real epsilon() const;

	BSpline eval(real t);
	BSpline operator()(real t);

	std::string toString() const;
private:
	BSpline m_origin, m_target;
	real m_epsilon;
	BSpline m_originAligned, m_targetAligned;
	BSpline m_buffer;
};
/*! @} */



/*! @name Reparametrization by Arc Length
 * @{
 */
class TINYSPLINECXX_API ChordLengths {
public:
	ChordLengths();
	ChordLengths(const ChordLengths &other);
	ChordLengths(ChordLengths &&other);
	virtual ~ChordLengths();

	ChordLengths &operator=(const ChordLengths &other);
	ChordLengths &operator=(ChordLengths &&other);

	BSpline spline() const;
	std::vector<real> knots() const;
	std::vector<real> lengths() const;
	TS_DEPRECATED std::vector<real> values() const;
	size_t size() const;

	real arcLength() const;
	real lengthToKnot(real len) const;
	real tToKnot(real t) const;
	std_real_vector_out equidistantKnotSeq(size_t num = 100) const;

	std::string toString() const;
private:
	BSpline m_spline;
	real *m_knots, *m_lengths;
	size_t m_size;
	ChordLengths(const BSpline &spline,
	             real *knots,
	             real *lengths,
	             size_t size);
	friend class BSpline;
};
/*! @} */

}
