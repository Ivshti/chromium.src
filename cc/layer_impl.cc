// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "CCLayerImpl.h"

#include "base/debug/trace_event.h"
#include "base/stringprintf.h"
#include "CCDebugBorderDrawQuad.h"
#include "CCLayerSorter.h"
#include "CCMathUtil.h"
#include "CCProxy.h"
#include "CCQuadSink.h"
#include "CCScrollbarAnimationController.h"
#include "CCSettings.h"

using WebKit::WebTransformationMatrix;

namespace cc {

CCLayerImpl::CCLayerImpl(int id)
    : m_parent(0)
    , m_maskLayerId(-1)
    , m_replicaLayerId(-1)
    , m_layerId(id)
    , m_layerTreeHostImpl(0)
    , m_anchorPoint(0.5, 0.5)
    , m_anchorPointZ(0)
    , m_scrollable(false)
    , m_shouldScrollOnMainThread(false)
    , m_haveWheelEventHandlers(false)
    , m_backgroundColor(0)
    , m_doubleSided(true)
    , m_layerPropertyChanged(false)
    , m_layerSurfacePropertyChanged(false)
    , m_masksToBounds(false)
    , m_contentsOpaque(false)
    , m_opacity(1.0)
    , m_preserves3D(false)
    , m_useParentBackfaceVisibility(false)
    , m_drawCheckerboardForMissingTiles(false)
    , m_useLCDText(false)
    , m_drawsContent(false)
    , m_forceRenderSurface(false)
    , m_isContainerForFixedPositionLayers(false)
    , m_fixedToContainerLayer(false)
    , m_renderTarget(0)
    , m_drawDepth(0)
    , m_drawOpacity(0)
    , m_drawOpacityIsAnimating(false)
    , m_debugBorderColor(0)
    , m_debugBorderWidth(0)
    , m_drawTransformIsAnimating(false)
    , m_screenSpaceTransformIsAnimating(false)
#ifndef NDEBUG
    , m_betweenWillDrawAndDidDraw(false)
#endif
    , m_layerAnimationController(CCLayerAnimationController::create(this))
{
    DCHECK(CCProxy::isImplThread());
    DCHECK(m_layerId > 0);
}

CCLayerImpl::~CCLayerImpl()
{
    DCHECK(CCProxy::isImplThread());
#ifndef NDEBUG
    DCHECK(!m_betweenWillDrawAndDidDraw);
#endif
}

void CCLayerImpl::addChild(scoped_ptr<CCLayerImpl> child)
{
    child->setParent(this);
    m_children.append(child.Pass());
}

void CCLayerImpl::removeFromParent()
{
    if (!m_parent)
        return;

    CCLayerImpl* parent = m_parent;
    m_parent = 0;

    for (size_t i = 0; i < parent->m_children.size(); ++i) {
        if (parent->m_children[i] == this) {
            parent->m_children.remove(i);
            return;
        }
    }
}

void CCLayerImpl::removeAllChildren()
{
    while (m_children.size())
        m_children[0]->removeFromParent();
}

void CCLayerImpl::clearChildList()
{
    m_children.clear();
}

void CCLayerImpl::createRenderSurface()
{
    DCHECK(!m_renderSurface);
    m_renderSurface = make_scoped_ptr(new CCRenderSurface(this));
    setRenderTarget(this);
}

bool CCLayerImpl::descendantDrawsContent()
{
    for (size_t i = 0; i < m_children.size(); ++i) {
        if (m_children[i]->drawsContent() || m_children[i]->descendantDrawsContent())
            return true;
    }
    return false;
}

scoped_ptr<CCSharedQuadState> CCLayerImpl::createSharedQuadState() const
{
    return CCSharedQuadState::create(m_drawTransform, m_visibleContentRect, m_drawableContentRect, m_drawOpacity, m_contentsOpaque);
}

void CCLayerImpl::willDraw(CCResourceProvider*)
{
#ifndef NDEBUG
    // willDraw/didDraw must be matched.
    DCHECK(!m_betweenWillDrawAndDidDraw);
    m_betweenWillDrawAndDidDraw = true;
#endif
}

void CCLayerImpl::didDraw(CCResourceProvider*)
{
#ifndef NDEBUG
    DCHECK(m_betweenWillDrawAndDidDraw);
    m_betweenWillDrawAndDidDraw = false;
#endif
}

void CCLayerImpl::appendDebugBorderQuad(CCQuadSink& quadList, const CCSharedQuadState* sharedQuadState, CCAppendQuadsData& appendQuadsData) const
{
    if (!hasDebugBorders())
        return;

    IntRect contentRect(IntPoint(), contentBounds());
    quadList.append(CCDebugBorderDrawQuad::create(sharedQuadState, contentRect, debugBorderColor(), debugBorderWidth()).PassAs<CCDrawQuad>(), appendQuadsData);
}

bool CCLayerImpl::hasContributingDelegatedRenderPasses() const
{
    return false;
}

CCRenderPass::Id CCLayerImpl::firstContributingRenderPassId() const
{
    return CCRenderPass::Id(0, 0);
}

CCRenderPass::Id CCLayerImpl::nextContributingRenderPassId(CCRenderPass::Id) const
{
    return CCRenderPass::Id(0, 0);
}

CCResourceProvider::ResourceId CCLayerImpl::contentsResourceId() const
{
    NOTREACHED();
    return 0;
}

FloatSize CCLayerImpl::scrollBy(const FloatSize& scroll)
{
    IntSize minDelta = -toSize(m_scrollPosition);
    IntSize maxDelta = m_maxScrollPosition - toSize(m_scrollPosition);
    // Clamp newDelta so that position + delta stays within scroll bounds.
    FloatSize newDelta = (m_scrollDelta + scroll).expandedTo(minDelta).shrunkTo(maxDelta);
    FloatSize unscrolled = m_scrollDelta + scroll - newDelta;

    if (m_scrollDelta == newDelta)
        return unscrolled;

    m_scrollDelta = newDelta;
    if (m_scrollbarAnimationController)
        m_scrollbarAnimationController->updateScrollOffset(this);
    noteLayerPropertyChangedForSubtree();

    return unscrolled;
}

CCInputHandlerClient::ScrollStatus CCLayerImpl::tryScroll(const IntPoint& screenSpacePoint, CCInputHandlerClient::ScrollInputType type) const
{
    if (shouldScrollOnMainThread()) {
        TRACE_EVENT0("cc", "CCLayerImpl::tryScroll: Failed shouldScrollOnMainThread");
        return CCInputHandlerClient::ScrollOnMainThread;
    }

    if (!screenSpaceTransform().isInvertible()) {
        TRACE_EVENT0("cc", "CCLayerImpl::tryScroll: Ignored nonInvertibleTransform");
        return CCInputHandlerClient::ScrollIgnored;
    }

    if (!nonFastScrollableRegion().isEmpty()) {
        bool clipped = false;
        FloatPoint hitTestPointInLocalSpace = CCMathUtil::projectPoint(screenSpaceTransform().inverse(), FloatPoint(screenSpacePoint), clipped);
        if (!clipped && nonFastScrollableRegion().contains(flooredIntPoint(hitTestPointInLocalSpace))) {
            TRACE_EVENT0("cc", "CCLayerImpl::tryScroll: Failed nonFastScrollableRegion");
            return CCInputHandlerClient::ScrollOnMainThread;
        }
    }

    if (type == CCInputHandlerClient::Wheel && haveWheelEventHandlers()) {
        TRACE_EVENT0("cc", "CCLayerImpl::tryScroll: Failed wheelEventHandlers");
        return CCInputHandlerClient::ScrollOnMainThread;
    }

    if (!scrollable()) {
        TRACE_EVENT0("cc", "CCLayerImpl::tryScroll: Ignored not scrollable");
        return CCInputHandlerClient::ScrollIgnored;
    }

    return CCInputHandlerClient::ScrollStarted;
}

bool CCLayerImpl::drawCheckerboardForMissingTiles() const
{
    return m_drawCheckerboardForMissingTiles && !Settings::backgroundColorInsteadOfCheckerboard();
}

IntRect CCLayerImpl::layerRectToContentRect(const WebKit::WebRect& layerRect)
{
    float widthScale = static_cast<float>(contentBounds().width()) / bounds().width();
    float heightScale = static_cast<float>(contentBounds().height()) / bounds().height();
    FloatRect contentRect(layerRect.x, layerRect.y, layerRect.width, layerRect.height);
    contentRect.scale(widthScale, heightScale);
    return enclosingIntRect(contentRect);
}

std::string CCLayerImpl::indentString(int indent)
{
    std::string str;
    for (int i = 0; i != indent; ++i)
        str.append("  ");
    return str;
}

void CCLayerImpl::dumpLayerProperties(std::string* str, int indent) const
{
    std::string indentStr = indentString(indent);
    str->append(indentStr);
    base::StringAppendF(str, "layer ID: %d\n", m_layerId);

    str->append(indentStr);
    base::StringAppendF(str, "bounds: %d, %d\n", bounds().width(), bounds().height());

    if (m_renderTarget) {
        str->append(indentStr);
        base::StringAppendF(str, "renderTarget: %d\n", m_renderTarget->m_layerId);
    }

    str->append(indentStr);
    base::StringAppendF(str, "drawTransform: %f, %f, %f, %f  //  %f, %f, %f, %f  //  %f, %f, %f, %f  //  %f, %f, %f, %f\n",
        m_drawTransform.m11(), m_drawTransform.m12(), m_drawTransform.m13(), m_drawTransform.m14(),
        m_drawTransform.m21(), m_drawTransform.m22(), m_drawTransform.m23(), m_drawTransform.m24(),
        m_drawTransform.m31(), m_drawTransform.m32(), m_drawTransform.m33(), m_drawTransform.m34(),
        m_drawTransform.m41(), m_drawTransform.m42(), m_drawTransform.m43(), m_drawTransform.m44());

    str->append(indentStr);
    base::StringAppendF(str, "drawsContent: %s\n", m_drawsContent ? "yes" : "no");
}

void sortLayers(std::vector<CCLayerImpl*>::iterator first, std::vector<CCLayerImpl*>::iterator end, CCLayerSorter* layerSorter)
{
    TRACE_EVENT0("cc", "CCLayerImpl::sortLayers");
    layerSorter->sort(first, end);
}

std::string CCLayerImpl::layerTreeAsText() const
{
    std::string str;
    dumpLayer(&str, 0);
    return str;
}

void CCLayerImpl::dumpLayer(std::string* str, int indent) const
{
    str->append(indentString(indent));
    base::StringAppendF(str, "%s(%s)\n", layerTypeAsString(), m_debugName.data());
    dumpLayerProperties(str, indent+2);
    if (m_replicaLayer) {
        str->append(indentString(indent+2));
        str->append("Replica:\n");
        m_replicaLayer->dumpLayer(str, indent+3);
    }
    if (m_maskLayer) {
        str->append(indentString(indent+2));
        str->append("Mask:\n");
        m_maskLayer->dumpLayer(str, indent+3);
    }
    for (size_t i = 0; i < m_children.size(); ++i)
        m_children[i]->dumpLayer(str, indent+1);
}

void CCLayerImpl::setStackingOrderChanged(bool stackingOrderChanged)
{
    // We don't need to store this flag; we only need to track that the change occurred.
    if (stackingOrderChanged)
        noteLayerPropertyChangedForSubtree();
}

bool CCLayerImpl::layerSurfacePropertyChanged() const
{
    if (m_layerSurfacePropertyChanged)
        return true;

    // If this layer's surface property hasn't changed, we want to see if
    // some layer above us has changed this property. This is done for the
    // case when such parent layer does not draw content, and therefore will
    // not be traversed by the damage tracker. We need to make sure that
    // property change on such layer will be caught by its descendants.
    CCLayerImpl* current = this->m_parent;
    while (current && !current->m_renderSurface) {
        if (current->m_layerSurfacePropertyChanged)
            return true;
        current = current->m_parent;
    }

    return false;
}

void CCLayerImpl::noteLayerPropertyChangedForSubtree()
{
    m_layerPropertyChanged = true;
    noteLayerPropertyChangedForDescendants();
}

void CCLayerImpl::noteLayerPropertyChangedForDescendants()
{
    for (size_t i = 0; i < m_children.size(); ++i)
        m_children[i]->noteLayerPropertyChangedForSubtree();
}

const char* CCLayerImpl::layerTypeAsString() const
{
    return "LayerChromium";
}

void CCLayerImpl::resetAllChangeTrackingForSubtree()
{
    m_layerPropertyChanged = false;
    m_layerSurfacePropertyChanged = false;

    m_updateRect = FloatRect();

    if (m_renderSurface)
        m_renderSurface->resetPropertyChangedFlag();

    if (m_maskLayer)
        m_maskLayer->resetAllChangeTrackingForSubtree();

    if (m_replicaLayer)
        m_replicaLayer->resetAllChangeTrackingForSubtree(); // also resets the replica mask, if it exists.

    for (size_t i = 0; i < m_children.size(); ++i)
        m_children[i]->resetAllChangeTrackingForSubtree();
}

bool CCLayerImpl::layerIsAlwaysDamaged() const
{
    return false;
}

int CCLayerImpl::id() const
{
     return m_layerId;
}

float CCLayerImpl::opacity() const
{
     return m_opacity;
}

void CCLayerImpl::setOpacityFromAnimation(float opacity)
{
    setOpacity(opacity);
}

const WebKit::WebTransformationMatrix& CCLayerImpl::transform() const
{
     return m_transform;
}

void CCLayerImpl::setTransformFromAnimation(const WebTransformationMatrix& transform)
{
    setTransform(transform);
}

void CCLayerImpl::setBounds(const IntSize& bounds)
{
    if (m_bounds == bounds)
        return;

    m_bounds = bounds;

    if (masksToBounds())
        noteLayerPropertyChangedForSubtree();
    else
        m_layerPropertyChanged = true;
}

void CCLayerImpl::setMaskLayer(scoped_ptr<CCLayerImpl> maskLayer)
{
    m_maskLayer = maskLayer.Pass();

    int newLayerId = m_maskLayer ? m_maskLayer->id() : -1;
    if (newLayerId == m_maskLayerId)
        return;

    m_maskLayerId = newLayerId;
    noteLayerPropertyChangedForSubtree();
}

void CCLayerImpl::setReplicaLayer(scoped_ptr<CCLayerImpl> replicaLayer)
{
    m_replicaLayer = replicaLayer.Pass();

    int newLayerId = m_replicaLayer ? m_replicaLayer->id() : -1;
    if (newLayerId == m_replicaLayerId)
        return;

    m_replicaLayerId = newLayerId;
    noteLayerPropertyChangedForSubtree();
}

void CCLayerImpl::setDrawsContent(bool drawsContent)
{
    if (m_drawsContent == drawsContent)
        return;

    m_drawsContent = drawsContent;
    m_layerPropertyChanged = true;
}

void CCLayerImpl::setAnchorPoint(const FloatPoint& anchorPoint)
{
    if (m_anchorPoint == anchorPoint)
        return;

    m_anchorPoint = anchorPoint;
    noteLayerPropertyChangedForSubtree();
}

void CCLayerImpl::setAnchorPointZ(float anchorPointZ)
{
    if (m_anchorPointZ == anchorPointZ)
        return;

    m_anchorPointZ = anchorPointZ;
    noteLayerPropertyChangedForSubtree();
}

void CCLayerImpl::setBackgroundColor(SkColor backgroundColor)
{
    if (m_backgroundColor == backgroundColor)
        return;

    m_backgroundColor = backgroundColor;
    m_layerPropertyChanged = true;
}

void CCLayerImpl::setFilters(const WebKit::WebFilterOperations& filters)
{
    if (m_filters == filters)
        return;

    m_filters = filters;
    noteLayerPropertyChangedForSubtree();
}

void CCLayerImpl::setBackgroundFilters(const WebKit::WebFilterOperations& backgroundFilters)
{
    if (m_backgroundFilters == backgroundFilters)
        return;

    m_backgroundFilters = backgroundFilters;
    m_layerPropertyChanged = true;
}

void CCLayerImpl::setMasksToBounds(bool masksToBounds)
{
    if (m_masksToBounds == masksToBounds)
        return;

    m_masksToBounds = masksToBounds;
    noteLayerPropertyChangedForSubtree();
}

void CCLayerImpl::setContentsOpaque(bool opaque)
{
    if (m_contentsOpaque == opaque)
        return;

    m_contentsOpaque = opaque;
    noteLayerPropertyChangedForSubtree();
}

void CCLayerImpl::setOpacity(float opacity)
{
    if (m_opacity == opacity)
        return;

    m_opacity = opacity;
    m_layerSurfacePropertyChanged = true;
}

bool CCLayerImpl::opacityIsAnimating() const
{
    return m_layerAnimationController->isAnimatingProperty(CCActiveAnimation::Opacity);
}

void CCLayerImpl::setPosition(const FloatPoint& position)
{
    if (m_position == position)
        return;

    m_position = position;
    noteLayerPropertyChangedForSubtree();
}

void CCLayerImpl::setPreserves3D(bool preserves3D)
{
    if (m_preserves3D == preserves3D)
        return;

    m_preserves3D = preserves3D;
    noteLayerPropertyChangedForSubtree();
}

void CCLayerImpl::setSublayerTransform(const WebTransformationMatrix& sublayerTransform)
{
    if (m_sublayerTransform == sublayerTransform)
        return;

    m_sublayerTransform = sublayerTransform;
    // sublayer transform does not affect the current layer; it affects only its children.
    noteLayerPropertyChangedForDescendants();
}

void CCLayerImpl::setTransform(const WebTransformationMatrix& transform)
{
    if (m_transform == transform)
        return;

    m_transform = transform;
    m_layerSurfacePropertyChanged = true;
}

bool CCLayerImpl::transformIsAnimating() const
{
    return m_layerAnimationController->isAnimatingProperty(CCActiveAnimation::Transform);
}

void CCLayerImpl::setDebugBorderColor(SkColor debugBorderColor)
{
    if (m_debugBorderColor == debugBorderColor)
        return;

    m_debugBorderColor = debugBorderColor;
    m_layerPropertyChanged = true;
}

void CCLayerImpl::setDebugBorderWidth(float debugBorderWidth)
{
    if (m_debugBorderWidth == debugBorderWidth)
        return;

    m_debugBorderWidth = debugBorderWidth;
    m_layerPropertyChanged = true;
}

bool CCLayerImpl::hasDebugBorders() const
{
    return SkColorGetA(m_debugBorderColor) && debugBorderWidth() > 0;
}

void CCLayerImpl::setContentBounds(const IntSize& contentBounds)
{
    if (m_contentBounds == contentBounds)
        return;

    m_contentBounds = contentBounds;
    m_layerPropertyChanged = true;
}

void CCLayerImpl::setScrollPosition(const IntPoint& scrollPosition)
{
    if (m_scrollPosition == scrollPosition)
        return;

    m_scrollPosition = scrollPosition;
    noteLayerPropertyChangedForSubtree();
}

void CCLayerImpl::setScrollDelta(const FloatSize& scrollDelta)
{
    if (m_scrollDelta == scrollDelta)
        return;

    m_scrollDelta = scrollDelta;
    noteLayerPropertyChangedForSubtree();
}

void CCLayerImpl::setImplTransform(const WebKit::WebTransformationMatrix& transform)
{
    if (m_implTransform == transform)
        return;

    m_implTransform = transform;
    noteLayerPropertyChangedForSubtree();
}

void CCLayerImpl::setDoubleSided(bool doubleSided)
{
    if (m_doubleSided == doubleSided)
        return;

    m_doubleSided = doubleSided;
    noteLayerPropertyChangedForSubtree();
}

Region CCLayerImpl::visibleContentOpaqueRegion() const
{
    if (contentsOpaque())
        return visibleContentRect();
    return Region();
}

void CCLayerImpl::didLoseContext()
{
}

void CCLayerImpl::setMaxScrollPosition(const IntSize& maxScrollPosition)
{
    m_maxScrollPosition = maxScrollPosition;

    if (!m_scrollbarAnimationController)
        return;
    m_scrollbarAnimationController->updateScrollOffset(this);
}

CCScrollbarLayerImpl* CCLayerImpl::horizontalScrollbarLayer() const
{
    return m_scrollbarAnimationController ? m_scrollbarAnimationController->horizontalScrollbarLayer() : 0;
}

void CCLayerImpl::setHorizontalScrollbarLayer(CCScrollbarLayerImpl* scrollbarLayer)
{
    if (!m_scrollbarAnimationController)
        m_scrollbarAnimationController = CCScrollbarAnimationController::create(this);
    m_scrollbarAnimationController->setHorizontalScrollbarLayer(scrollbarLayer);
    m_scrollbarAnimationController->updateScrollOffset(this);
}

CCScrollbarLayerImpl* CCLayerImpl::verticalScrollbarLayer() const
{
    return m_scrollbarAnimationController ? m_scrollbarAnimationController->verticalScrollbarLayer() : 0;
}

void CCLayerImpl::setVerticalScrollbarLayer(CCScrollbarLayerImpl* scrollbarLayer)
{
    if (!m_scrollbarAnimationController)
        m_scrollbarAnimationController = CCScrollbarAnimationController::create(this);
    m_scrollbarAnimationController->setVerticalScrollbarLayer(scrollbarLayer);
    m_scrollbarAnimationController->updateScrollOffset(this);
}

}
