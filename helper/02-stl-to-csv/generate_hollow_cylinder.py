# Filename: generate_hollow_cylinder.py

import trimesh
import numpy as np
import os

def create_hollow_cylinder_stl(
    inner_radius: float,
    outer_radius: float,
    height: float,
    output_filename: str = "hollow_cylinder.stl",
    center: tuple[float, float, float] = (0, 0, 0),
    axis: tuple[float, float, float] = (0, 0, 1),
    sections: int = 32 # Controls the smoothness of the cylinder approximation
):
    """
    Generates an STL file for a hollow cylinder (pipe) using trimesh.

    Args:
        inner_radius: The inner radius of the cylinder. Must be less than outer_radius.
        outer_radius: The outer radius of the cylinder.
        height: The height of the cylinder along its axis.
        output_filename: The name of the STL file to save.
        center: The center point of the cylinder's base.
        axis: The direction vector of the cylinder's central axis.
        sections: The number of facets used to approximate the circular shape.
                  Higher values result in a smoother cylinder but larger file size.

    Returns:
        True if the STL file was generated successfully, False otherwise.
    """
    if inner_radius >= outer_radius:
        print("Error: Inner radius must be less than outer radius.")
        return False
    if height <= 0:
        print("Error: Height must be positive.")
        return False
    if inner_radius <= 0:
        print("Error: Inner radius must be positive.")
        return False

    print(f"Generating hollow cylinder:")
    print(f"  Inner Radius: {inner_radius}")
    print(f"  Outer Radius: {outer_radius}")
    print(f"  Height: {height}")
    print(f"  Center: {center}")
    print(f"  Axis: {axis}")
    print(f"  Sections: {sections}")
    print(f"  Output file: {output_filename}")

    try:
        # 1. Create the outer cylinder
        # We use a transform to align the cylinder primitive (default along Z)
        # to the specified center and axis.
        # First, create the base cylinder along Z at origin
        outer_cylinder_base = trimesh.primitives.Cylinder(
            radius=outer_radius, height=height, sections=sections
        )

        # 2. Create the inner cylinder (same parameters but smaller radius)
        inner_cylinder_base = trimesh.primitives.Cylinder(
            radius=inner_radius, height=height, sections=sections
        )

        # Determine the transformation matrix to align the cylinder
        z_axis = np.array([0, 0, 1])
        target_axis = trimesh.util.unitize(np.array(axis))
        # Rotation from Z to target axis
        rotation_matrix = trimesh.geometry.rotation_matrix(
            np.arccos(np.dot(z_axis, target_axis)),
            trimesh.util.unitize(np.cross(z_axis, target_axis))
        ) if not np.allclose(z_axis, target_axis) else np.identity(4) # Handle case where axis is already Z

        # Translation to the center (assuming center is the midpoint of the height along the axis)
        translation_vector = np.array(center)
        transform_matrix = trimesh.transformations.translation_matrix(translation_vector) @ rotation_matrix


        # Apply the transform to both cylinders
        outer_cylinder = outer_cylinder_base.apply_transform(transform_matrix)
        inner_cylinder = inner_cylinder_base.apply_transform(transform_matrix)


        # 3. Perform boolean difference (Outer - Inner)
        # This requires a backend like 'blender', 'scad', or using 'rtree' dependency.
        # Trimesh will attempt to use the best available one.
        print("Performing boolean difference (this might take a moment)...")
        hollow_cylinder = outer_cylinder.difference(inner_cylinder)
        print("Boolean difference complete.")

        # Check if the difference operation was successful (sometimes fails)
        if not isinstance(hollow_cylinder, trimesh.Trimesh) or hollow_cylinder.is_empty:
             print("Error: Boolean difference failed. The resulting mesh is empty or invalid.")
             print("Ensure trimesh's boolean dependencies (like 'rtree' or Blender/OpenSCAD) are correctly installed.")
             # As a fallback, maybe export the outer cylinder? Or just fail.
             # print("Saving outer cylinder as fallback...")
             # outer_cylinder.export(output_filename)
             # return True
             return False


        # 4. Export the resulting mesh to STL
        print(f"Exporting to {output_filename}...")
        hollow_cylinder.export(file_obj=output_filename)
        print("Export complete.")
        return True

    except ImportError as e:
        print(f"Error: Missing dependency for trimesh - {e}")
        print("Please install the required libraries (e.g., 'pip install rtree').")
        return False
    except Exception as e:
        print(f"An unexpected error occurred: {e}")
        return False

# --- Example Usage ---
if __name__ == "__main__":
    # Define parameters for the hollow cylinder
    inner_r = 0.8  # Example inner radius
    outer_r = 1.0  # Example outer radius
    cyl_height = 5.0 # Example height
    output_stl = "example_pipe.stl"
    cyl_center = (0, 0, 0) # Center the pipe base at origin
    cyl_axis = (0, 0, 1)   # Align along the Z-axis

    # Optional: Create output directory if it doesn't exist
    # output_dir = "output_stls"
    # if not os.path.exists(output_dir):
    #     os.makedirs(output_dir)
    # full_output_path = os.path.join(output_dir, output_stl)
    full_output_path = output_stl # Save in current directory

    # Generate the STL
    success = create_hollow_cylinder_stl(
        inner_radius=inner_r,
        outer_radius=outer_r,
        height=cyl_height,
        output_filename=full_output_path,
        center=cyl_center,
        axis=cyl_axis,
        sections=64 # Increase sections for a smoother look
    )

    if success:
        print(f"\nSuccessfully generated '{full_output_path}'")
    else:
        print(f"\nFailed to generate STL file.")
