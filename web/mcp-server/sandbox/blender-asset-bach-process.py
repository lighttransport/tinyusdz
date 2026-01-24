import bpy
import os
from pathlib import Path
import bmesh

def get_asset_libraries():
    """Get all configured asset libraries"""
    preferences = bpy.context.preferences
    return preferences.filepaths.asset_libraries

def list_asset_libraries_detailed():
    """List all asset libraries with detailed information"""
    print("=" * 60)
    print("ASSET LIBRARIES DETAILED")
    print("=" * 60)
    
    libraries = get_asset_libraries()
    
    for i, lib in enumerate(libraries):
        print(f"\n{i+1}. Library: {lib.name}")
        print(f"   Path: {lib.path}")
        print(f"   Import Method: {lib.import_method}")
        print(f"   Path exists: {os.path.exists(lib.path)}")
        
        # Count blend files in library
        if os.path.exists(lib.path):
            blend_files = list(Path(lib.path).glob("**/*.blend"))
            print(f"   Blend files found: {len(blend_files)}")
            
            # Show some example files
            if blend_files:
                print("   Sample files:")
                for blend_file in blend_files[:5]:  # Show first 5
                    print(f"     - {blend_file.name}")
                if len(blend_files) > 5:
                    print(f"     ... and {len(blend_files) - 5} more")
                    

list_asset_libraries_detailed();         