catch {load vtktcl}
# user interface command widget
source ../../examplesTcl/vtkInt.tcl

# create a rendering window and renderer
vtkRenderer ren1
vtkRenderWindow renWin
    renWin AddRenderer ren1
vtkRenderWindowInteractor iren
    iren SetRenderWindow renWin

# create cones of varying resolution
vtkConeSource cone0
  cone0 SetResolution 0
vtkConeSource cone1
  cone1 SetResolution 1
vtkConeSource cone8
  cone8 SetResolution 8

vtkPolyDataMapper cone0Mapper
  cone0Mapper SetInput [cone0 GetOutput]
vtkActor cone0Actor
  cone0Actor SetMapper cone0Mapper

vtkPolyDataMapper cone1Mapper
  cone1Mapper SetInput [cone1 GetOutput]
vtkActor cone1Actor
  cone1Actor SetMapper cone1Mapper

vtkPolyDataMapper cone8Mapper
  cone8Mapper SetInput [cone8 GetOutput]
vtkActor cone8Actor
  cone8Actor SetMapper cone8Mapper

# assign our actor to the renderer
ren1 AddActor cone0Actor
ren1 AddActor cone1Actor
ren1 AddActor cone8Actor
ren1 SetBackground .5 .5 .5
[ren1 GetActiveCamera] Dolly 1.2
renWin SetSize 301 91
cone0Actor SetPosition -2 0 0
cone8Actor SetPosition 2 0 0

[cone0Actor GetProperty] SetDiffuseColor 1 0 0
[cone1Actor GetProperty] SetDiffuseColor 0 1 0
[cone8Actor GetProperty] BackfaceCullingOn
[cone8Actor GetProperty] SetDiffuseColor 0 0 1

# enable user interface interactor
iren SetUserMethod {wm deiconify .vtkInteract}
iren Initialize

#renWin SetFileName "coneResolution.tcl.ppm"
#renWin SaveImageAsPPM

# prevent the tk window from showing up then start the event loop
wm withdraw .

