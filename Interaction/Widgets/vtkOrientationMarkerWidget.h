// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
/**
 * @class   vtkOrientationMarkerWidget
 * @brief   2D widget for manipulating a marker prop
 *
 * This class provides support for interactively manipulating the position,
 * size, and apparent orientation of a prop that represents an orientation
 * marker.  This class works by adding its internal renderer to an external
 * "parent" renderer on a different layer.  The input orientation marker is
 * rendered as an overlay on the parent renderer and, thus, appears superposed
 * over all props in the parent's scene.  The camera view of the orientation
 * the marker is made to match that of the parent's by means of an observer
 * mechanism, giving the illusion that the orientation of the marker reflects
 * that of the prop(s) in the parent's scene.
 *
 * The widget listens to left mouse button and mouse movement events. It will
 * change the cursor shape based on its location. If the cursor is over the
 * overlay renderer, it will change the cursor shape to a SIZEALL shape
 * or to a resize corner shape (e.g., SIZENW) if the cursor is near a corner.
 * If the left mouse button is pressed and held down while moving, the overlay
 * renderer, and hence, the orientation marker, is resized or moved.  I the case
 * of a resize operation, releasing the left mouse button causes the widget
 * to enforce its renderer to be square.  The diagonally opposite corner to the
 * one moved is repositioned such that all edges of the renderer have the same
 * length: the minimum.
 *
 * To use this object, there are two key steps: 1) invoke SetInteractor() with
 * the argument of the method a vtkRenderWindowInteractor, and 2) invoke
 * SetOrientationMarker with an instance of vtkProp (see caveats below).
 * Specifically, vtkAxesActor and vtkAnnotatedCubeActor are two classes
 * designed to work with this class.  A composite orientation marker can be
 * generated by adding instances of vtkAxesActor and vtkAnnotatedCubeActor to a
 * vtkPropAssembly, which can then be set as the input orientation marker.
 * The widget can be also be set up in a non-interactive fashion by setting
 * Ineractive to Off and sizing/placing the overlay renderer in its parent
 * renderer by calling the widget's SetViewport method.
 *
 * @par Thanks:
 * This class was based originally on Paraview's vtkPVAxesWidget.
 *
 * @warning
 * The input orientation marker prop should calculate its bounds as though they
 * are symmetric about it's origin.  This must currently be done to correctly
 * implement the camera synchronization between the ivar renderer and the
 * renderer associated with the set interactor.  Importantly, the InteractorStyle
 * associated with the interactor must be of the type vtkInteractorStyle*Camera.
 * Where desirable, the parent renderer should be set by the SetDefaultRenderer
 * method.  The parent renderer's number of layers is modified to 2 where
 * required.
 *
 * @sa
 * vtkInteractorObserver vtkXYPlotWidget vtkScalarBarWidget vtkAxesActor
 * vtkAnnotatedCubeActor
 */

#ifndef vtkOrientationMarkerWidget_h
#define vtkOrientationMarkerWidget_h

#include "vtkInteractionWidgetsModule.h" // For export macro
#include "vtkInteractorObserver.h"
#include "vtkWrappingHints.h" // For VTK_MARSHALAUTO

VTK_ABI_NAMESPACE_BEGIN
class vtkActor2D;
class vtkPolyData;
class vtkProp;
class vtkOrientationMarkerWidgetObserver;
class vtkRenderer;

class VTKINTERACTIONWIDGETS_EXPORT VTK_MARSHALAUTO vtkOrientationMarkerWidget
  : public vtkInteractorObserver
{
public:
  static vtkOrientationMarkerWidget* New();
  vtkTypeMacro(vtkOrientationMarkerWidget, vtkInteractorObserver);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  ///@{
  /**
   * Set/get the orientation marker to be displayed in this widget.
   */
  virtual void SetOrientationMarker(vtkProp* prop);
  vtkGetObjectMacro(OrientationMarker, vtkProp);
  ///@}

  /**
   * Enable/disable the widget. Default is 0 (disabled).
   */
  void SetEnabled(int) override;

  /**
   * Callback to keep the camera for the orientation marker up to date with the
   * camera in the parent renderer.
   */
  void ExecuteCameraUpdateEvent(vtkObject* o, unsigned long event, void* calldata);

  ///@{
  /**
   * Set/get whether to allow this widget to be interactively moved/scaled.
   * Default is On.
   */
  void SetInteractive(vtkTypeBool interact);
  vtkGetMacro(Interactive, vtkTypeBool);
  vtkBooleanMacro(Interactive, vtkTypeBool);
  ///@}

  ///@{
  /**
   * Set/get the color of the outline of this widget.  The outline is visible
   * when (in interactive mode) the cursor is over this widget.
   * Default is white (1,1,1).
   */
  void SetOutlineColor(double r, double g, double b);
  double* GetOutlineColor() VTK_SIZEHINT(3);
  ///@}

  ///@{
  /**
   * Set/get the viewport to position/size this widget.
   * Coordinates are expressed as (xmin,ymin,xmax,ymax), where each
   * coordinate is 0 <= coordinate <= 1.0.
   * Default is bottom left corner (0,0,0.2,0.2).
   * Note that this viewport is scaled with respect to the viewport of the
   * current renderer i.e. if the viewport of the current renderer is
   * (0.5, 0.5, 0.75, 0.75) and Viewport is set to (0, 0, 1, 1), the orientation
   * marker will be confined to a viewport of (0.5, 0.5, 0.75, 0.75) in the
   * render window.
   * \sa SetCurrentRenderer()
   */
  vtkSetVector4Macro(Viewport, double);
  vtkGetVector4Macro(Viewport, double);
  ///@}

  ///@{
  /**
   * The tolerance representing the distance to the widget (in pixels)
   * in which the cursor is considered to be on the widget, or on a
   * widget feature (e.g., a corner point or edge).
   */
  vtkSetClampMacro(Tolerance, int, 1, 10);
  vtkGetMacro(Tolerance, int);
  ///@}

  ///@{
  /**
   * The zoom factor to modify the size of the marker within the widget.
   * Default is 1.0.
   */
  vtkSetClampMacro(Zoom, double, 0.1, 10.0);
  vtkGetMacro(Zoom, double);
  ///@}

  ///@{
  /**
   * Need to reimplement this->Modified() because of the
   * vtkSetVector4Macro/vtkGetVector4Macro use
   */
  void Modified() override;
  ///@}

  ///@{
  /**
   * Ends any in progress interaction and resets border visibility
   */
  void EndInteraction() override;
  ///@}

  ///@{
  /**
   * Set/get whether the widget should constrain the size to be within the min and max limits.
   * Default is off (unconstrained).
   */
  void SetShouldConstrainSize(vtkTypeBool shouldConstrainSize);
  vtkGetMacro(ShouldConstrainSize, vtkTypeBool);
  ///@}

  ///@{
  /**
   * Sets the minimum and maximum dimension (width and height) size limits for the widget.
   * Validates the sizes are within tolerances before setting; ignoring otherwise.
   * Default is 20, 500.
   * Returns whether the sizes are valid and correctly set (true), or invalid (false).
   */
  bool SetSizeConstraintDimensionSizes(int minDimensionSize, int maxDimensionSize);
  ///@}

  ///@{
  /**
   * Returns the minimum dimension (width and height) size limit in pixels for the widget.
   */
  vtkGetMacro(MinDimensionSize, int);
  ///@}

  ///@{
  /**
   * Returns the maximum dimension (width and height) size limit in pixels for the widget.
   */
  vtkGetMacro(MaxDimensionSize, int);
  ///@}

protected:
  vtkOrientationMarkerWidget();
  ~vtkOrientationMarkerWidget() override;

  vtkRenderer* Renderer;
  vtkProp* OrientationMarker;
  vtkPolyData* Outline;
  vtkActor2D* OutlineActor;

  unsigned long StartEventObserverId;

  static void ProcessEvents(
    vtkObject* object, unsigned long event, void* clientdata, void* calldata);

  // ProcessEvents() dispatches to these methods.
  virtual void OnLeftButtonDown();
  virtual void OnLeftButtonUp();
  virtual void OnMouseMove();

  // observer to update the renderer's camera
  vtkOrientationMarkerWidgetObserver* Observer;

  vtkTypeBool Interactive;
  int Tolerance;
  int Moving;
  double Zoom = 1.0;

  // viewport to position/size this widget
  double Viewport[4];

  // used to compute relative movements
  int StartPosition[2];

  // Manage the state of the widget
  int State;
  enum WidgetState
  {
    Outside = 0,
    Inside,
    Translating,
    AdjustingP1,
    AdjustingP2,
    AdjustingP3,
    AdjustingP4
  };

  // Whether the min/max size constraints should be applied.
  vtkTypeBool ShouldConstrainSize = 0;
  // The minimum dimension size to be allowed for width and height.
  int MinDimensionSize = 20;
  // The maximum dimension size to be allowed for width and height.
  int MaxDimensionSize = 500;

  // use to determine what state the mouse is over, edge1 p1, etc.
  // returns a state from the WidgetState enum above
  virtual int ComputeStateBasedOnPosition(int X, int Y, int* pos1, int* pos2);

  // set the cursor to the correct shape based on State argument
  virtual void SetCursor(int state);

  // adjust the viewport depending on state
  void MoveWidget(int X, int Y);
  void ResizeTopLeft(int X, int Y);
  void ResizeTopRight(int X, int Y);
  void ResizeBottomLeft(int X, int Y);
  void ResizeBottomRight(int X, int Y);

  void SquareRenderer();
  void UpdateOutline();

  // Used to reverse compute the Viewport ivar with respect to the current
  // renderer viewport
  void UpdateViewport();
  // Used to compute and set the viewport on the internal renderer based on the
  // Viewport ivar. The computed viewport will be with respect to the whole
  // render window
  void UpdateInternalViewport();

  // Resize the widget if it is outside of the current size constraints,
  // or if the widget is not square.
  void ResizeToFitSizeConstraints();

private:
  vtkOrientationMarkerWidget(const vtkOrientationMarkerWidget&) = delete;
  void operator=(const vtkOrientationMarkerWidget&) = delete;

  // set up the actors and observers created by this widget
  void SetupWindowInteraction();
  // tear down up the actors and observers created by this widget
  void TearDownWindowInteraction();
};

VTK_ABI_NAMESPACE_END
#endif
