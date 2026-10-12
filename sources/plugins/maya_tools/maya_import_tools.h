#pragma once
#ifndef __MAYA_IMPORT_TOOLS_H__
#define __MAYA_IMPORT_TOOLS_H__

#include <string>
#include <vector>
#ifdef _MSC_VER
#if _MSC_VER >= 1900
#define _SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS
#endif
#include <hash_map>
#else
#include <map>
#endif
#include <maya/MStatus.h>

#include "xr_sdk_version.h"

namespace xray_re {
	class xr_object;
	class xr_bone;
	class xr_mesh;
	class xr_skl_motion;
	class xr_bone_motion;
	class xr_obj_motion;
	class xr_partition;
	class xr_surface;
};

class MObject;
class MPlug;
class MSelectionList;
class MFnIkJoint;
class MFnDependencyNode;
class MTimeArray;
class MDoubleArray;

#ifdef _MSC_VER
typedef stdext::hash_map<std::string, MObject> maya_object_map;
#else
typedef std::map<std::string, MObject> maya_object_map;
#endif
typedef maya_object_map::iterator maya_object_map_it;
typedef std::pair<std::string, MObject> maya_object_pair;

class maya_import_tools {
public:
			maya_import_tools(const MString& options = "");
			~maya_import_tools();
			maya_import_tools(const xray_re::xr_object* object, MStatus* return_status = 0, const MString& options = "");

	MStatus		import_object(const xray_re::xr_object* object);
	// character_obj selects the target: a character gets Trax clips (legacy),
	// a null object - Time Editor animation sources for the joints found by
	// lookup_target(). owner (the loaded file) maps bone part and start bone
	// indices of the motions to names.
	MStatus		import_motions(const std::vector<xray_re::xr_skl_motion*>& motions,
					MObject& character_obj, const xray_re::xr_object* owner = 0);

	// keep OMF bone parts on the skeleton root for the OMF export
	void		store_bone_parts(const std::vector<xray_re::xr_partition*>& partitions);
	// keep the motion references of a skeletal object/model on the skeleton root
	void		store_motion_refs(const std::string& refs);

	// .anm: a camera parent-constrained to an animated locator
	MStatus		import_obj_motion(const xray_re::xr_obj_motion* motion, const MString& default_name);

	void		reset_animation_state() const;
	MObject		lookup_target(MStatus* return_status = 0);

	// drop pending callbacks before the plug-in is unloaded
	static void	uninitialize();

private:
	MStatus		import_motion(const xray_re::xr_skl_motion* motion, MObject& character_obj);
	MStatus		import_motions_te(const std::vector<xray_re::xr_skl_motion*>& motions,
					const xray_re::xr_object* owner);
	void		sample_bone_motion(const xray_re::xr_skl_motion* smotion,
					const xray_re::xr_bone_motion* bmotion, MFnIkJoint& joint_fn,
					MTimeArray times[6], MDoubleArray values[6]) const;
	void		collect_joints(const MObject& root_obj);
	// Motions of an OMF that keeps its bones as bare ids have no bone
	// names: the i-th bone motion goes to the i-th bone of the skin, the
	// order the game and the OMF export use.
	void		collect_bone_order();
	maya_object_map_it find_joint(const xray_re::xr_bone_motion* bmotion, size_t index);
	void		create_mark_curves(const xray_re::xr_skl_motion* smotion, const MObject& root_obj,
					std::vector<std::pair<MPlug, MPlug> >& saved, MSelectionList& members);
	void		store_motion_params(MFnDependencyNode& source_fn,
					const xray_re::xr_skl_motion* smotion, int order,
					const xray_re::xr_object* owner);
	MStatus		import_surface(const xray_re::xr_surface* surface, MObject& texture_obj);
	MStatus		import_bone(const xray_re::xr_bone* bone, MObject& parent_obj);
	MStatus		import_mesh(const xray_re::xr_mesh* mesh, const std::vector<xray_re::xr_bone*>& bones);

	void		set_default_options(void);
	MStatus		parse_options(const MString& options);

private:
	maya_object_map	m_sets;
	maya_object_map	m_joints;
	std::vector<std::string> m_bone_order;	// see collect_bone_order()

	xray_re::sdk_version m_target_sdk;
	bool m_use_joint_orient;
};

#endif
