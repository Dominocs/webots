// Copyright 1996-2019 Cyberbotics Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "WbSphere.hpp"

#include "WbBoundingSphere.hpp"
#include "WbField.hpp"
#include "WbFieldChecker.hpp"
#include "WbMatter.hpp"
#include "WbNodeUtilities.hpp"
#include "WbRay.hpp"
#include "WbResizeManipulator.hpp"
#include "WbSFBool.hpp"
#include "WbSFInt.hpp"
#include "WbSimulationState.hpp"
#include "WbTransform.hpp"
#include "WbWrenRenderingContext.hpp"

#include <wren/config.h>
#include <wren/renderable.h>
#include <wren/static_mesh.h>
#include <wren/transform.h>

#include <ode/ode.h>

#include <cmath>

void WbSphere::init() {
  mRadius = findSFDouble("radius");
  mSubdivision = findSFInt("subdivision");
  mIco = findSFBool("ico");
  mResizeConstraint = WbWrenAbstractResizeManipulator::UNIFORM;
}

WbSphere::WbSphere(WbTokenizer *tokenizer) : WbGeometry("Sphere", tokenizer) {
  init();
  if (tokenizer == NULL) {
    mRadius->setValueNoSignal(0.1);
    mIco->setValueNoSignal(true);
  }
}

WbSphere::WbSphere(const WbSphere &other) : WbGeometry(other) {
  init();
}

WbSphere::WbSphere(const WbNode &other) : WbGeometry(other) {
  init();
}

WbSphere::~WbSphere() {
  wr_static_mesh_delete(mWrenMesh);
}

void WbSphere::postFinalize() {
  WbGeometry::postFinalize();

  connect(mRadius, &WbSFDouble::changed, this, &WbSphere::updateRadius);
  connect(mSubdivision, &WbSFInt::changed, this, &WbSphere::updateMesh);
  connect(mIco, &WbSFBool::changed, this, &WbSphere::updateMesh);
}

void WbSphere::createWrenObjects() {
  WbGeometry::createWrenObjects();

  buildWrenMesh();

  if (isInBoundingObject())
    connect(WbWrenRenderingContext::instance(), &WbWrenRenderingContext::lineScaleChanged, this, &WbSphere::updateLineScale);

  emit wrenObjectsCreated();
}

void WbSphere::setResizeManipulatorDimensions() {
  WbVector3 scale(radius(), radius(), radius());
  WbTransform *transform = upperTransform();
  if (transform)
    scale *= transform->matrix().scale();

  if (isAValidBoundingObject())
    scale *= 1.0f + (wr_config_get_line_scale() / LINE_SCALE_FACTOR);

  resizeManipulator()->updateHandleScale(scale.ptr());
  updateResizeHandlesSize();
}

void WbSphere::createResizeManipulator() {
  mResizeManipulator =
    new WbRegularResizeManipulator(uniqueId(), (WbWrenAbstractResizeManipulator::ResizeConstraint)mResizeConstraint);
}

bool WbSphere::areSizeFieldsVisibleAndNotRegenerator() const {
  const WbField *const radius = findField("radius", true);
  return WbNodeUtilities::isVisible(radius) && !WbNodeUtilities::isTemplateRegeneratorField(radius);
}

void WbSphere::exportNodeFields(WbVrmlWriter &writer) const {
  WbGeometry::exportNodeFields(writer);
  if (writer.isX3d()) {
    writer << " subdivision=\'" << mSubdivision->value() << ',' << mSubdivision->value() << "\'";
    writer << " ico=\'" << (mIco->value() ? "true" : "false") << "\'";
  }
}

bool WbSphere::sanitizeFields() {
  if (mIco->value()) {
    WbFieldChecker::checkIntInRangeWithIncludedBounds(this, mSubdivision, 1, 5, 1);
  } else
    WbFieldChecker::checkIntInRangeWithIncludedBounds(this, mSubdivision, 3, 32, 24);

  if (WbFieldChecker::checkDoubleIsPositive(this, mRadius, 1.0))
    return false;

  return true;
}

void WbSphere::buildWrenMesh() {
  WbGeometry::deleteWrenRenderable();

  wr_static_mesh_delete(mWrenMesh);
  mWrenMesh = NULL;

  if (!sanitizeFields())
    return;

  WbGeometry::computeWrenRenderable();

  const bool createOutlineMesh = isInBoundingObject();
  mWrenMesh = wr_static_mesh_unit_sphere_new(mSubdivision->value(), mIco->value(), createOutlineMesh);

  // Restore pickable state
  setPickable(isPickable());

  wr_renderable_set_mesh(mWrenRenderable, WR_MESH(mWrenMesh));

  if (createOutlineMesh)
    updateLineScale();
  else
    updateScale();
}

void WbSphere::updateRadius() {
  if (!sanitizeFields())
    return;

  if (isInBoundingObject())
    updateLineScale();
  else
    updateScale();

  if (isInBoundingObject())
    applyToOdeData();

  if (mBoundingSphere && !isInBoundingObject())
    mBoundingSphere->setOwnerSizeChanged();

  if (resizeManipulator() && resizeManipulator()->isAttached())
    setResizeManipulatorDimensions();

  emit changed();
}

void WbSphere::updateMesh() {
  if (!sanitizeFields())
    return;

  buildWrenMesh();

  emit changed();
}

void WbSphere::updateLineScale() {
  if (!isAValidBoundingObject() || !sanitizeFields())
    return;

  const float offset = wr_config_get_line_scale() / LINE_SCALE_FACTOR;
  const float scaledRadius = static_cast<float>(mRadius->value() * (1.0 + offset));
  const float scale[] = {scaledRadius, scaledRadius, scaledRadius};
  wr_transform_set_scale(wrenNode(), scale);
}

void WbSphere::updateScale() {
  if (!sanitizeFields())
    return;

  const float scaledRadius = static_cast<float>(mRadius->value());
  const float scale[] = {scaledRadius, scaledRadius, scaledRadius};
  wr_transform_set_scale(wrenNode(), scale);
}

void WbSphere::rescale(const WbVector3 &scale) {
  if (scale.x() != 1.0)
    setRadius(radius() * scale.x());
  else if (scale.y() != 1.0)
    setRadius(radius() * scale.y());
  else if (scale.z() != 1.0)
    setRadius(radius() * scale.z());
}

/////////////////
// ODE objects //
/////////////////

dGeomID WbSphere::createOdeGeom(dSpaceID space) {
  if (mRadius->value() <= 0.0) {
    warn(tr("'radius' must be positive when used in 'boundingObject'."));
    return NULL;
  }

  if (WbNodeUtilities::findUpperMatter(this)->nodeType() == WB_NODE_FLUID)
    checkFluidBoundingObjectOrientation();

  return dCreateSphere(space, scaledRadius());
}

void WbSphere::applyToOdeData(bool correctSolidMass) {
  if (mOdeGeom == NULL)
    return;

  assert(dGeomGetClass(mOdeGeom) == dSphereClass);
  dGeomSphereSetRadius(mOdeGeom, scaledRadius());

  if (correctSolidMass)
    applyToOdeMass();
}

double WbSphere::scaledRadius() const {
  const WbVector3 &scale = absoluteScale();
  return fabs(mRadius->value() * std::max(std::max(scale.x(), scale.y()), scale.z()));
}

bool WbSphere::isSuitableForInsertionInBoundingObject(bool warning) const {
  const bool invalidRadius = mRadius->value() <= 0.0;
  if (warning && invalidRadius)
    warn(tr("'radius' must be positive when used in 'boundingObject'."));
  return !invalidRadius;
}

bool WbSphere::isAValidBoundingObject(bool checkOde, bool warning) const {
  const bool admissible = WbGeometry::isAValidBoundingObject(checkOde, warning);
  return admissible && isSuitableForInsertionInBoundingObject(admissible && warning);
}
/////////////////
// Ray Tracing //
/////////////////

bool WbSphere::pickUVCoordinate(WbVector2 &uv, const WbRay &ray, int textureCoordSet) const {
  WbVector3 collisionPoint;
  bool collisionExists = computeCollisionPoint(collisionPoint, ray);
  if (!collisionExists)
    return false;

  WbTransform *transform = upperTransform();
  WbVector3 pointOnTexture(collisionPoint);
  if (transform) {
    pointOnTexture = transform->matrix().pseudoInversed(collisionPoint);
    pointOnTexture /= absoluteScale();
  }

  double theta = atan2(pointOnTexture.x(), pointOnTexture.z()) + M_PI;
  double u = theta / (2 * M_PI);
  double radius = scaledRadius();
  double v = 1 - (pointOnTexture.y() + radius) / (2 * radius);

  // result
  uv.setXy(u, v);
  return true;
}

double WbSphere::computeDistance(const WbRay &ray) const {
  WbVector3 collisionPoint;
  bool collisionExists = computeCollisionPoint(collisionPoint, ray);
  if (!collisionExists)
    return -1;

  WbVector3 d = ray.origin() - collisionPoint;
  return d.length();
}

bool WbSphere::computeCollisionPoint(WbVector3 &point, const WbRay &ray) const {
  WbVector3 center;
  const WbTransform *const transform = upperTransform();
  if (transform)
    center = transform->matrix().translation();
  double radius = scaledRadius();

  // distance from sphere
  const std::pair<bool, double> result = ray.intersects(center, radius, true);

  point = ray.origin() + result.second * ray.direction();
  return result.first;
}

void WbSphere::recomputeBoundingSphere() const {
  assert(mBoundingSphere);
  mBoundingSphere->set(WbVector3(), scaledRadius());
}

////////////////////////
// Friction Direction //
////////////////////////

WbVector3 WbSphere::computeFrictionDirection(const WbVector3 &normal) const {
  warn(tr("A Sphere is used in a Bounding object using an asymmetric friction. Sphere does not support asymmetric friction"));
  return WbVector3(0, 0, 0);
}
