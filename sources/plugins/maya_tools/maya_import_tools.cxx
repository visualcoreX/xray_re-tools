// This code is inspired by Maya plugins from NifTools and Valve SDK.

#define NOMINMAX
#include <maya/MTypes.h>
#if (_MSC_VER < 1600) && (MAYA_API_VERSION >= 20180000) && (MAYA_API_VERSION < 20220000)
#include <maya/MCppCompat.h>
#endif
#include <maya/MAnimControl.h>
#include <maya/MDGModifier.h>
#include <maya/MDagPath.h>
#include <maya/MDagPathArray.h>
#include <maya/MDistance.h>
#include <maya/MEulerRotation.h>
#include <maya/MFloatArray.h>
#include <maya/MFnAnimCurve.h>
#include <maya/MFnCharacter.h>
#include <maya/MFnClip.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnIkJoint.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnMesh.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnSet.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MFnSkinCluster.h>
#include <maya/MFnTransform.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MIntArray.h>
#include <maya/MItDag.h>
#include <maya/MItDependencyGraph.h>
#include <maya/MItDependencyNodes.h>
#include <maya/MItMeshEdge.h>
#include <maya/MItMeshPolygon.h>
#include <maya/MPlugArray.h>
#include <maya/MPointArray.h>
#include <maya/MProgressWindow.h>
#include <maya/MNamespace.h>
#include <maya/MSelectionList.h>
#include <maya/MQuaternion.h>
#include <maya/MObjectHandle.h>
#include <maya/MSceneMessage.h>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>
#include "maya_import_tools.h"
#include "maya_progress.h"
#include "xr_object.h"
#include "xr_envelope.h"
#include "xr_obj_motion.h"
#include "xr_file_system.h"

using namespace xray_re;

static MObject create_texture(const std::string& texture, MStatus* return_status = 0);

maya_import_tools::maya_import_tools(const MString& options)
{
	set_default_options();
	parse_options(options);
}

maya_import_tools::maya_import_tools(const xray_re::xr_object* object, MStatus* return_status, const MString& options)
{
	set_default_options();
	parse_options(options);

	MStatus status = import_object(object);
	if (return_status)
		*return_status = status;
}

maya_import_tools::~maya_import_tools()
{
	MGlobal::clearSelectionList();
}

static MString make_maya_name(const std::string& base, const char* old_suffix, const char* suffix = "")
{
	std::string::size_type pos = base.rfind(old_suffix);
	if (pos == std::string::npos)
		return MString(base.c_str()) + suffix;
	else
		return MString(base.c_str(), int(pos & INT_MAX)) + suffix;
}

MStatus maya_import_tools::import_object(const xr_object* object)
{
	MStatus status = MS::kSuccess;
	const xr_bone_vec& bones = object->bones();

	start_progress(bones.size(), "Importing bones");
	for (xr_bone_vec_cit it = bones.begin(), end = bones.end(); it != end; ++it) {
		if ((*it)->is_root()) {
			status = import_bone(*it, MObject::kNullObj);
			break;
		}
	}
	end_progress();
	if (!status)
		return status;

	maya_object_map shared_textures;
	start_progress(object->surfaces().size(), "Importing surfaces");
	for (xr_surface_vec_cit it = object->surfaces().begin(),
			end = object->surfaces().end(); it != end; ++it) {
		xr_surface* surface = *it;
		MObject& texture_obj = shared_textures[surface->texture()];
		if (texture_obj.isNull()) {
			texture_obj = create_texture(surface->texture(), &status);
			if (!status)
				break;
		}
		if (!(status = import_surface(surface, texture_obj)))
			break;
		advance_progress();
	}
	end_progress();
	if (!status)
		return status;
	shared_textures.clear();

	start_progress(object->meshes().size(), "Importing meshes");
	for (xr_mesh_vec_cit it = object->meshes().begin(),
			end = object->meshes().end(); it != end; ++it) {
		if (!(status = import_mesh(*it, bones)))
			break;
		advance_progress();
	}
	end_progress();
	if (!status)
		return status;

	if (!bones.empty() && !object->motion_refs().empty())
		store_motion_refs(object->motion_refs());

	// no character set: the Time Editor refuses attributes that belong to one
	if (!bones.empty() && !object->motions().empty()) {
		reset_animation_state();
		MObject no_character;
		status = import_motions(object->motions(), no_character, object);
	}

	return status;
}

static MPlug get_free_plug(MFn::Type filter, const char* list_name)
{
	MItDependencyNodes it(filter);
	if (!it.isDone()) {
		MFnDependencyNode dep_fn(it.thisNode());
		MStatus status;
		MPlug list_plug = dep_fn.findPlug(list_name, &status);
		if (status) {
			for (unsigned i = 0;; ++i) {
				MPlug plug = list_plug.elementByLogicalIndex(i, &status);
				CHECK_MSTATUS(status);
				if (!plug.isConnected())
					return plug;
			}
		}
	}
	return MPlug();
}

static MObject create_texture(const std::string& texture, MStatus* return_status)
{
	std::string path(texture);
	xr_file_system& fs = xr_file_system::instance();
	if (fs.file_exist(PA_GAME_TEXTURES, path.append(".dds"))) {
		std::string full_path;
		fs.resolve_path(PA_GAME_TEXTURES, path, full_path);
		path = full_path;
	}
	for (std::string::size_type i = 0; (i = path.find('\\', i)) != std::string::npos; ++i)
		path[i] = '/';

	std::string::size_type i = texture.rfind('\\');
	MString maya_name(i != std::string::npos ? texture.c_str() + i + 1 : texture.c_str());

	MStatus status;
	MFnDependencyNode texture_fn;
	MObject texture_obj = texture_fn.create("file", maya_name += "_F", &status);
	if (!status) {
		if (return_status)
			*return_status = status;
		return MObject::kNullObj;
	}
	texture_fn.findPlug("ftn").setValue(path.c_str());

	MDGModifier dg_modifier;
	dg_modifier.connect(texture_fn.findPlug("msg"), get_free_plug(MFn::kTextureList, "textures"));
	dg_modifier.doIt();

	MFnDependencyNode p2dt_fn;
	p2dt_fn.create("place2dTexture", maya_name += "_P2DT", &status);
	if (!status) {
		if (return_status)
			*return_status = status;
		return MObject::kNullObj;
	}
	dg_modifier.connect(p2dt_fn.findPlug("c"), texture_fn.findPlug("c"));
	dg_modifier.connect(p2dt_fn.findPlug("tf"), texture_fn.findPlug("tf"));
	dg_modifier.connect(p2dt_fn.findPlug("rf"), texture_fn.findPlug("rf"));
	dg_modifier.connect(p2dt_fn.findPlug("mu"), texture_fn.findPlug("mu"));
	dg_modifier.connect(p2dt_fn.findPlug("mv"), texture_fn.findPlug("mv"));
	dg_modifier.connect(p2dt_fn.findPlug("s"), texture_fn.findPlug("s"));
	dg_modifier.connect(p2dt_fn.findPlug("wu"), texture_fn.findPlug("wu"));
	dg_modifier.connect(p2dt_fn.findPlug("wv"), texture_fn.findPlug("wv"));
	dg_modifier.connect(p2dt_fn.findPlug("re"), texture_fn.findPlug("re"));
	dg_modifier.connect(p2dt_fn.findPlug("of"), texture_fn.findPlug("of"));
	dg_modifier.connect(p2dt_fn.findPlug("r"), texture_fn.findPlug("ro"));
	dg_modifier.connect(p2dt_fn.findPlug("n"), texture_fn.findPlug("n"));
	dg_modifier.connect(p2dt_fn.findPlug("vt1"), texture_fn.findPlug("vt1"));
	dg_modifier.connect(p2dt_fn.findPlug("vt2"), texture_fn.findPlug("vt2"));
	dg_modifier.connect(p2dt_fn.findPlug("vt3"), texture_fn.findPlug("vt3"));
	dg_modifier.connect(p2dt_fn.findPlug("vc1"), texture_fn.findPlug("vc1"));
	dg_modifier.connect(p2dt_fn.findPlug("o"), texture_fn.findPlug("uv"));
	dg_modifier.connect(p2dt_fn.findPlug("ofs"), texture_fn.findPlug("fs"));
	dg_modifier.doIt();

	if (return_status)
		*return_status = MS::kSuccess;
	return texture_obj;
}

static void set_xraymtl_attr(MFnDependencyNode& dep_fn, const char* name, const std::string& value)
{
	MStatus status;
	MPlug plug = dep_fn.findPlug(name, &status);
	if (status) {
		MFnEnumAttribute attr_fn(plug.attribute(), &status);
		if (status) {
			std::string temp(value);
			for (std::string::size_type i = 0; (i = temp.find('\\', i)) != std::string::npos; ++i)
				temp[i] = '/';
			short index = attr_fn.fieldIndex(temp.c_str(), &status);
			if (!status) {
				msg("xray_re: can't set attribute %s to %s", name, value.c_str());
				MGlobal::displayError(MString("xray_re: can't set attribute ") +
					name + " to " + value.c_str());
				return;
			}
			plug.setShort(index);
		}
	}
}

MStatus maya_import_tools::import_surface(const xr_surface* surface, MObject& texture_obj)
{
	MStatus status;
	MFnDependencyNode shader_fn;
	MObject shader_obj = shader_fn.create("XRayMtl",
			make_maya_name(surface->name(), "_S", "_M"), &status);
	if (!status) {
		msg("xray_re: can't create shader");
		MGlobal::displayError("xray_re: can't create shader");
		return status;
	}

	set_xraymtl_attr(shader_fn, "xrayGameMaterial", surface->gamemtl());
	set_xraymtl_attr(shader_fn, "xrayEngineShader", surface->eshader());
	set_xraymtl_attr(shader_fn, "xrayCompilerShader", surface->cshader());
	shader_fn.findPlug("xrayDoubleSide").setBool(surface->two_sided());

	MDGModifier dg_modifier;
	dg_modifier.connect(shader_fn.findPlug("msg"), get_free_plug(MFn::kShaderList, "shaders"));
	dg_modifier.doIt();

	MFnSet set_fn;
	MObject set_obj = set_fn.create(MSelectionList(), MFnSet::kRenderableOnly, &status);
	if (!status) {
		msg("xray_re: can't create shading group");
		MGlobal::displayError("xray_re: can't create shading group");
		return status;
	}
	set_fn.setName(make_maya_name(surface->name(), "_S", "_SG"));

	MPlug ss_plug = set_fn.findPlug("ss", &status);
	MPlugArray connected_plugs;
	if (ss_plug.connectedTo(connected_plugs, true, false)) {
		for (unsigned i = connected_plugs.length(); i != 0;)
			dg_modifier.disconnect(connected_plugs[--i], ss_plug);
		dg_modifier.doIt();
	}
	dg_modifier.connect(shader_fn.findPlug("oc"), ss_plug);
	dg_modifier.doIt();

	MFnDependencyNode texture_fn(texture_obj);
	dg_modifier.connect(texture_fn.findPlug("oc"), shader_fn.findPlug("c"));
	dg_modifier.doIt();

	// connect hw materialInfo's texture plug to workaround Maya's blurry default setting
	MItDependencyGraph dg_it(set_fn.findPlug("msg"), MFn::kMaterialInfo,
			MItDependencyGraph::kDownstream, MItDependencyGraph::kDepthFirst,
			MItDependencyGraph::kNodeLevel);
	if (!dg_it.isDone()) {
		MFnDependencyNode info_fn(dg_it.thisNode());
		status = dg_modifier.connect(texture_fn.findPlug("msg"),
				info_fn.findPlug("t").elementByLogicalIndex(0));
		CHECK_MSTATUS(status);
		dg_modifier.doIt();
	}

	m_sets[surface->name()] = set_obj;

	return status;
}

MStatus maya_import_tools::import_bone(const xr_bone* bone, MObject& parent_obj)
{
	MStatus status;

	MFnIkJoint joint_fn;
	MObject joint_obj = joint_fn.create(parent_obj, &status);
	if (!status) {
		msg("xray_re: can't create joint %s", bone->name().c_str());
		MGlobal::displayError(MString("xray_re: can't create joint ") + bone->name().c_str());
		return status;
	}
	joint_fn.setName(bone->name().c_str());

	const fvector3& t = bone->bind_offset();
	double x = MDistance(t.x, MDistance::kMeters).asCentimeters();
	double y = MDistance(t.y, MDistance::kMeters).asCentimeters();
	double z = MDistance(t.z, MDistance::kMeters).asCentimeters();
	joint_fn.setTranslation(MVector(x, y, -z), MSpace::kTransform);

	const fvector3& r = bone->bind_rotate();
	if (m_use_joint_orient) {
		joint_fn.setOrientation(MEulerRotation(-r.x, -r.y, r.z, MEulerRotation::kZXY).asQuaternion());
	}else {
		joint_fn.setRotation(MEulerRotation(-r.x, -r.y, r.z, MEulerRotation::kZXY));
	}

	m_joints.insert(maya_object_pair(bone->name(), joint_obj));
	advance_progress();

	for (xr_bone_vec_cit it = bone->children().begin(), end = bone->children().end();
			it != end; ++it) {
		if (!(status = import_bone(*it, joint_obj)))
			break;
	}
	return status;
}

static inline void append_uvs(const std::vector<fvector2>& uvs, MFloatArray& u_values, MFloatArray& v_values)
{
	unsigned offset = u_values.length(), size = unsigned(uvs.size() & UINT_MAX) + offset;
	u_values.setLength(size);
	v_values.setLength(size);
	for (std::vector<fvector2>::const_iterator it = uvs.begin(), end = uvs.end(); it != end; ++it) {
		u_values.set(it->x, offset);
		v_values.set(1.f - it->y, offset);
		++offset;
	}
}

MStatus maya_import_tools::import_mesh(const xr_mesh* mesh, const xr_bone_vec& bones)
{
	MStatus status;

	MFnTransform transform_fn;
	MObject transform_obj = transform_fn.create(MObject::kNullObj, &status);
	if (!status) {
		msg("xray_re: can't create mesh %s transform", mesh->name().c_str());
		MGlobal::displayError(MString("xray_re: can't create mesh ") +
			mesh->name().c_str() + " transform");
		return status;
	}
	transform_fn.setName(make_maya_name(mesh->name(), "Shape"));

	const std::vector<fvector3>& points = mesh->points();
	MPointArray vertices;
	vertices.setLength(unsigned(points.size() & UINT_MAX));
	uint_fast32_t vert_idx = 0;
	for (std::vector<fvector3>::const_iterator it = points.begin(), end = points.end();
			it != end; ++it) {
		// hard-coded conversion to centimeters for compatibility with official GSC
		// export plug-in.
		double x = MDistance(it->x, MDistance::kMeters).asCentimeters();
		double y = MDistance(it->y, MDistance::kMeters).asCentimeters();
		double z = MDistance(it->z, MDistance::kMeters).asCentimeters();
		vertices.set(unsigned(vert_idx++), x, y, -z);
	}

	const xr_vmap_vec& vmaps = mesh->vmaps();

	MFloatArray u_values, v_values;
	unsigned* uv_offsets = new unsigned[vmaps.size()];
	for (xr_vmap_vec_cit it = vmaps.begin(), end = vmaps.end(); it != end; ++it) {
		if ((*it)->type() == xr_vmap::VMT_UV) {
			const xr_uv_vmap* vmap = static_cast<const xr_uv_vmap*>(*it);
			uv_offsets[it - vmaps.begin()] = u_values.length();
			append_uvs(vmap->uvs(), u_values, v_values);
		}
	}

	const lw_face_vec& faces = mesh->faces();
	MIntArray poly_connects, uv_ids;
	poly_connects.setLength(unsigned(faces.size() & UINT_MAX)*3);
	uv_ids.setLength(poly_connects.length());
	const lw_vmref_vec& vmrefs = mesh->vmrefs();
	vert_idx = 0;
	for (lw_face_vec_cit it = faces.begin(), end = faces.end(); it != end; ++it) {
		for (uint_fast32_t i = 3; i != 0;) {
			poly_connects.set(it->v[--i], unsigned(vert_idx));
			const lw_vmref& vmref = vmrefs[it->ref[i]];
			for (lw_vmref::const_iterator it1 = vmref.begin(), end1 = vmref.end(); it1 != end1; ++it1) {
				if (vmaps[it1->vmap]->type() == xr_vmap::VMT_UV)
					uv_ids.set(it1->offset + uv_offsets[it1->vmap], unsigned(vert_idx));
			}
			++vert_idx;
		}
	}
	delete[] uv_offsets;

	MIntArray poly_counts(unsigned(faces.size() & UINT_MAX), 3);

	MFnMesh mesh_fn;
	MObject mesh_obj = mesh_fn.create(vertices.length(), poly_counts.length(),
			vertices, poly_counts, poly_connects, u_values, v_values,
			transform_obj, &status);
	if (!status) {
		msg("xray_re: can't create mesh %s", mesh->name().c_str());
		MGlobal::displayError(MString("xray_re: can't create mesh ") + mesh->name().c_str());
		return status;
	}
	mesh_fn.setName(make_maya_name(mesh->name(), "Shape", "Shape"));

	// temporary safety measure
	if (mesh_fn.numPolygons() != faces.size()) {
		msg("xray_re: mesh polygon count was changed");
		MGlobal::displayError("xray_re: mesh polygon count was changed");
//		return MS::kFailure;
	}

	status = mesh_fn.assignUVs(poly_counts, uv_ids, 0);
	CHECK_MSTATUS(status);

	if (mesh->sgroups().empty()) {
		for (MItMeshEdge it(mesh_obj); !it.isDone(); it.next())
			it.setSmoothing(false);
	} else {
		
		MIntArray connected;
		const std::vector<uint32_t>& sgroups = mesh->sgroups();
		if (m_target_sdk <= xray_re::SDK_VER_0_4) {
			if (mesh->flags() & EMF_3DSMAX) {
				for (MItMeshEdge it(mesh_obj); !it.isDone(); it.next()) {
					it.getConnectedFaces(connected, &status);
					it.setSmoothing(status && connected.length() == 2 &&
							(sgroups[connected[0]] & sgroups[connected[1]]) != 0);
				}
			} else {
				for (MItMeshEdge it(mesh_obj); !it.isDone(); it.next()) {
					it.getConnectedFaces(connected, &status);
					it.setSmoothing(status && connected.length() == 2 &&
							sgroups[connected[0]] != EMESH_NO_SG &&
							sgroups[connected[0]] == sgroups[connected[1]]);
				}
			}
		} else {
			for (MItMeshPolygon it(mesh_obj); !it.isDone(); it.next()) {
				status = it.getEdges(connected);
				if (!status)
					return MS::kFailure;

				mesh_fn.setEdgeSmoothing(connected[0], !(sgroups[it.index()] & 0x2));
				mesh_fn.setEdgeSmoothing(connected[1], !(sgroups[it.index()] & 0x1));
				mesh_fn.setEdgeSmoothing(connected[2], !(sgroups[it.index()] & 0x4));
			}
		}
	}

	MDagPath mesh_path;
	if (!(status = mesh_fn.getPath(mesh_path)))
		return MS::kFailure;

	MDGModifier dg_modifier;
	for (xr_surfmap_vec_cit it = mesh->surfmaps().begin(), end = mesh->surfmaps().end();
			it != end; ++it) {
		const xr_surfmap* smap = *it;
		MObject& set_obj = m_sets[smap->surface->name()];
		if (set_obj.isNull()) {
			msg("xray_re: null shading group object");
			MGlobal::displayError("xray_re: null shading group object");
			return MS::kFailure;
		}

		MIntArray shaded_faces;
		shaded_faces.setLength(unsigned(smap->faces.size() & UINT_MAX));
		std::vector<uint32_t>::const_reverse_iterator it1 = smap->faces.rbegin();
		for (unsigned i = shaded_faces.length(); i != 0; ++it1)
			shaded_faces.set(*it1, --i);

		MFnSingleIndexedComponent component_fn;
		MObject component_obj = component_fn.create(MFn::kMeshPolygonComponent, &status);
		if (!status || !(status = component_fn.addElements(shaded_faces)))
			return status;

		MSelectionList members;
		if (!(status = members.add(mesh_path, component_obj)))
			return status;

		MFnSet set_fn(set_obj);
		if (!(status = set_fn.addMembers(members)))
			return status;
	}

	if (mesh_fn.numVertices() != points.size()) {
		msg("xray_re: mesh vertex count was changed");
		MGlobal::displayError("xray_re: mesh vertex count was changed");
		return MS::kFailure;
	}

	if (!bones.empty()) {
		MString command("skinCluster -mi 4 -tsb ");
		for (maya_object_map_it it = m_joints.begin(), end = m_joints.end(); it != end; ++it) {
			MFnIkJoint joint_fn(it->second, &status);
			CHECK_MSTATUS(status);
			command += joint_fn.partialPathName();
			command += " ";
		}
		command += mesh_fn.partialPathName();

		MStringArray result;
		status = MGlobal::executeCommand(command, result);
		if (!status) {
			msg("xray_re: can't create skin cluster");
			MGlobal::displayError("xray_re: can't create skin cluster");
			return status;
		}

		MSelectionList selection_list;
		status = selection_list.add(result[0]);
		CHECK_MSTATUS(status);
		MObject skin_obj;
		selection_list.getDependNode(0, skin_obj);
		MFnSkinCluster skin_fn(skin_obj, &status);
		if (!status)
			return status;

		MFnSingleIndexedComponent component_fn;
		MObject component_obj = component_fn.create(MFn::kMeshVertComponent, &status);
		if (!status)
			return status;
		status = mesh_fn.setObject(mesh_path);
		if (!status)
			return status;
		MIntArray vertex_indices;
		vertex_indices.setLength(mesh_fn.numVertices());
		for (unsigned i = vertex_indices.length(); i != 0;) {
			--i;
			vertex_indices[i] = i;
		}
		status = component_fn.addElements(vertex_indices);
		CHECK_MSTATUS(status);

		unsigned num_bones = unsigned(bones.size() & UINT_MAX), bone_idx = 0;
		MIntArray influence_indices(num_bones);
		MDoubleArray vertex_weights(vertex_indices.length()*num_bones, 0);
		for (xr_bone_vec_cit it = bones.begin(), end = bones.end(); it != end; ++it, ++bone_idx) {
			const xr_bone* bone = *it;

			maya_object_map_it joint_it = m_joints.find(bone->name());
			xr_assert(joint_it != m_joints.end());
			MFnIkJoint joint_fn(joint_it->second, &status);
			CHECK_MSTATUS(status);
			MDagPath joint_path;
			status = joint_fn.getPath(joint_path);
			CHECK_MSTATUS(status);
			unsigned influence_idx = skin_fn.indexForInfluenceObject(joint_path, &status);
			CHECK_MSTATUS(status);
			influence_indices[bone_idx] = influence_idx;

			const xr_weight_vmap* vmap = 0;
			MString bone_name(bone->name().c_str());
			bone_name.toLowerCase();

			for (xr_vmap_vec_cit it1 = vmaps.begin(), end1 = vmaps.end(); it1 != end1; ++it1) {
				MString vmap_name((*it1)->name().c_str());
				vmap_name.toLowerCase();

				if (bone_name == vmap_name) {
					vmap = static_cast<const xr_weight_vmap*>(*it1);
					break;
				}
			}
			if (vmap) {
				xr_assert(vmap->type() == xr_vmap::VMT_WEIGHT);
				std::vector<uint32_t>::const_iterator v = vmap->vertices().begin();
				for (std::vector<float>::const_iterator w = vmap->weights().begin(),
						w_end = vmap->weights().end(); w != w_end; ++v, ++w) {
					vertex_weights[*v * num_bones + bone_idx] = *w;
				}
			}
		}
		status = skin_fn.setWeights(mesh_path, component_obj,
				influence_indices, vertex_weights, false);
		CHECK_MSTATUS(status);
	}
	return MS::kSuccess;
}

void maya_import_tools::collect_joints(const MObject& root_obj)
{
	// a null root means the whole scene
	MItDag it(MItDag::kDepthFirst, MFn::kJoint);
	if (!root_obj.isNull())
		it.reset(root_obj, MItDag::kDepthFirst, MFn::kJoint);
	for (; !it.isDone(); it.next()) {
		MObject joint_obj = it.currentItem();
		MFnIkJoint joint_fn(joint_obj);
		MObject& slot = m_joints[joint_fn.name().asChar()];
		if (slot.isNull())
			slot = joint_obj;
	}
}

// The skin cluster bound to most of the target joints gives the bone order.
void maya_import_tools::collect_bone_order()
{
	m_bone_order.clear();
	size_t best = 0;
	for (MItDependencyNodes it(MFn::kSkinClusterFilter); !it.isDone(); it.next()) {
		MFnSkinCluster skin_fn(it.thisNode());
		MDagPathArray influences;
		skin_fn.influenceObjects(influences);
		std::vector<std::string> names;
		size_t found = 0;
		for (unsigned i = 0; i != influences.length(); ++i) {
			std::string name = MFnDependencyNode(influences[i].node()).name().asChar();
			maya_object_map_it joint_it = m_joints.find(name);
			if (joint_it != m_joints.end() && joint_it->second == influences[i].node())
				++found;
			names.push_back(name);
		}
		if (found > best) {
			best = found;
			m_bone_order.swap(names);
		}
	}
}

maya_object_map_it maya_import_tools::find_joint(const xr_bone_motion* bmotion, size_t index)
{
	if (!bmotion->name().empty())
		return m_joints.find(bmotion->name());
	if (index < m_bone_order.size())
		return m_joints.find(m_bone_order[index]);
	return m_joints.end();
}

// A selected character means the legacy Trax workflow. Otherwise motions go
// to the Time Editor: a selected joint picks its skeleton, an empty selection
// takes all joints in the scene.
MObject maya_import_tools::lookup_target(MStatus* return_status)
{
	MSelectionList selection_list;
	MStatus status = MGlobal::getActiveSelectionList(selection_list);
	if (selection_list.isEmpty()) {
		collect_joints(MObject::kNullObj);
		if (m_joints.empty()) {
			MGlobal::displayError("xray_re: there are no joints in the scene");
			status = MS::kInvalidParameter;
		}
		if (return_status)
			*return_status = status;
		return MObject::kNullObj;
	}
	MObject character_obj;
	selection_list.getDependNode(0, character_obj);
	if (character_obj.hasFn(MFn::kJoint)) {
		// climb to the skeleton root
		MDagPath path;
		selection_list.getDagPath(0, path);
		MDagPath parent(path);
		while (parent.pop() && parent.length() > 0 && parent.hasFn(MFn::kJoint))
			path = parent;
		collect_joints(path.node());
		if (return_status)
			*return_status = MS::kSuccess;
		return MObject::kNullObj;
	}
	if (!character_obj.hasFn(MFn::kCharacter)) {
		MGlobal::displayError("xray_re: select a joint of the skeleton (or a character for Trax clips)");
		if (return_status)
			*return_status = MS::kInvalidParameter;
		return MObject::kNullObj;
	}
	MFnCharacter character_fn(character_obj, &status);
	MPlugArray member_plugs;
	character_fn.getMemberPlugs(member_plugs);
	for (unsigned i = member_plugs.length(); i != 0;) {
		MObject member_obj = member_plugs[--i].node();
		if (!member_obj.hasFn(MFn::kJoint))
			continue;
		MFnIkJoint joint_fn(member_obj);
		MObject& joint_obj = m_joints[joint_fn.name().asChar()];
		if (joint_obj.isNull()) {
			joint_obj = member_obj;
			msg("xray_re: found bone %s", joint_fn.name().asChar());
			MGlobal::displayInfo(MString("xray_re: found bone ") + joint_fn.name().asChar());
		}
	}
	if (return_status)
		*return_status = status;
	return character_obj;
}

void maya_import_tools::reset_animation_state() const
{
	MTime::setUIUnit(MTime::kNTSCFrame);
	MAnimControl::setMinTime(MTime(0, MTime::kSeconds));
	MAnimControl::setAnimationStartTime(MTime(0, MTime::kSeconds));
}

static MFnAnimCurve::InfinityType maya_infinity(uint8_t behaviour)
{
	switch (behaviour) {
	case xr_envelope::BEH_RESET:
		return MFnAnimCurve::kConstant;		// FIXME
	default:
	case xr_envelope::BEH_CONSTANT:
		return MFnAnimCurve::kConstant;
	case xr_envelope::BEH_REPEAT:
		return MFnAnimCurve::kCycle;
	case xr_envelope::BEH_OSCILLATE:
		return MFnAnimCurve::kOscillate;
	case xr_envelope::BEH_OFFSET:
		return MFnAnimCurve::kConstant;		// FIXME
	case xr_envelope::BEH_LINEAR:
		return MFnAnimCurve::kLinear;
	}
}

static inline void append_key(MTimeArray& times, MDoubleArray& values, double time, double value)
{
	unsigned size = values.length();
	if (size == 0 || values[size-1] != value) {
		times.append(MTime(time, MTime::kSeconds));
		values.append(value);
	}
}

static const MString k_plug_names[6] = { "tx", "ty", "tz", "rx", "ry", "rz" };

void maya_import_tools::sample_bone_motion(const xr_skl_motion* smotion, const xr_bone_motion* bmotion,
		MFnIkJoint& joint_fn, MTimeArray times[6], MDoubleArray values[6]) const
{
	MQuaternion bind_rotation;
	joint_fn.getOrientation(bind_rotation);

	MQuaternion inv_bind_rotation = bind_rotation.inverse();

	double fps = smotion->fps();

	// rebuild from scratch to handle differently-ordered rotation and non-step shape.
	// FIXME: it might be better to force the conversion into bezier curves.
	for (int32_t frame = smotion->frame_start(), frame_end = smotion->frame_end();
			frame < frame_end; ++frame) {
		// divide here for better precision.
		double time = frame/fps;

		fvector3 offs, rot;
		bmotion->evaluate(float(time), offs, rot);

		append_key(times[0], values[0], time, MDistance(offs.x, MDistance::kMeters).asCentimeters());
		append_key(times[1], values[1], time, MDistance(offs.y, MDistance::kMeters).asCentimeters());
		append_key(times[2], values[2], time, MDistance(-offs.z, MDistance::kMeters).asCentimeters());

		MEulerRotation maya_rot(-rot.x, -rot.y, rot.z, MEulerRotation::kZXY);
		maya_rot.reorderIt(MEulerRotation::kXYZ);

		if (m_use_joint_orient) {
			MQuaternion maya_rot_quat = maya_rot.asQuaternion() * inv_bind_rotation;
			maya_rot = maya_rot_quat.asEulerRotation();
		}

		append_key(times[3], values[3], time, maya_rot.x);
		append_key(times[4], values[4], time, maya_rot.y);
		append_key(times[5], values[5], time, maya_rot.z);
	}
}

MStatus maya_import_tools::import_motion(const xray_re::xr_skl_motion* smotion, MObject& character_obj)
{
	MStatus status;
	MDGModifier dg_modifier;

	MFnCharacter character_fn(character_obj);

	MString clip_name(smotion->name().c_str());

	double fps = smotion->fps();
	double start_time = smotion->frame_start() / fps;
	int32_t frame_span = smotion->frame_end() - smotion->frame_start();
	double duration = (frame_span > 1 ? frame_span - 1 : 0) / fps;

	MFnClip clip_fn;
	MObject clip_obj = clip_fn.createSourceClip(MTime(start_time, MTime::kSeconds),
		MTime(duration, MTime::kSeconds), dg_modifier, &status);
	if (!status) {
		msg("xray_re: can't create clip %s", clip_name.asChar());
		MGlobal::displayError(MString("xray_re: can't create clip ") + clip_name.asChar());
		return status;
	}
	clip_fn.setName(clip_name);
	dg_modifier.doIt();

	MTimeArray times[6];
	MDoubleArray values[6];

	for (xr_bone_motion_vec_cit it = smotion->bone_motions().begin(),
			end = smotion->bone_motions().end(); it != end; ++it) {
		const xr_bone_motion* bmotion = *it;
		// missing bones are reported once by import_motions()
		maya_object_map_it joint_it = find_joint(bmotion, size_t(it - smotion->bone_motions().begin()));
		if (joint_it == m_joints.end())
			continue;

		MFnIkJoint joint_fn(joint_it->second, &status);
		CHECK_MSTATUS(status);

		MString name(clip_name);
		name += '_';
		name += bmotion->name().c_str();
		name += '_';

		sample_bone_motion(smotion, bmotion, joint_fn, times, values);

		const xr_envelope* const* envelopes = bmotion->envelopes();
		for (uint_fast32_t i = 6; i != 0;) {
			MFnAnimCurve curve_fn;
			MObject curve_obj = curve_fn.create(--i >= 3 ?
					MFnAnimCurve::kAnimCurveTA : MFnAnimCurve::kAnimCurveTL,
					0, &status);
			CHECK_MSTATUS(status);
			curve_fn.setName(name + k_plug_names[i]);
			curve_fn.setPreInfinityType(maya_infinity(envelopes[i]->pre_behaviour()));
			curve_fn.setPostInfinityType(maya_infinity(envelopes[i]->post_behaviour()));
			status = curve_fn.addKeys(&times[i], &values[i],
					MFnAnimCurve::kTangentStep, MFnAnimCurve::kTangentStep);
			CHECK_MSTATUS(status);
			status = character_fn.addCurveToClip(curve_obj, clip_obj,
					joint_fn.findPlug(k_plug_names[i]), dg_modifier);
			CHECK_MSTATUS(status);
			times[i].clear();
			values[i].clear();
		}
	}

	status = character_fn.attachSourceToCharacter(clip_obj, dg_modifier);
	CHECK_MSTATUS(status);
	dg_modifier.doIt();

	return MS::kSuccess;
}

// Maya node names allow only [A-Za-z0-9_] and can't start with a digit.
static MString maya_node_name(const std::string& name)
{
	std::string result(name);
	for (std::string::iterator it = result.begin(), end = result.end(); it != end; ++it) {
		if (!isalnum(uint8_t(*it)) && *it != '_')
			*it = '_';
	}
	if (result.empty() || isdigit(uint8_t(result[0])))
		result.insert(0, "m_");
	return MString(result.c_str());
}

static MString quoted(const MString& s) { return MString("\"") + s + "\""; }

// When file import is over, Maya prefixes the targets of anim sources created
// during the import with the source name, as if they came from the file
// ("idle:bone.translateX"), and the Time Editor can't find the objects then.
static std::vector<MObjectHandle> g_new_sources;
static MCallbackId g_after_import_cb = 0;

static void fix_source_targets(void*)
{
	MMessage::removeCallback(g_after_import_cb);
	g_after_import_cb = 0;
	for (std::vector<MObjectHandle>::iterator it = g_new_sources.begin(), end = g_new_sources.end();
			it != end; ++it) {
		if (!it->isValid())
			continue;
		MFnDependencyNode source_fn(it->object());
		// a source in a skeleton's namespace gets that namespace instead
		// ("wpn_body_motions:wpn_body.translateX")
		std::string full = source_fn.name().asChar();
		std::string::size_type colon = full.rfind(':');
		std::vector<std::string> prefixes;
		prefixes.push_back(full + ":");
		if (colon != std::string::npos)
			prefixes.push_back(full.substr(0, colon + 1));
		MPlug animation = source_fn.findPlug("animation", true);
		MObject target_attr = source_fn.attribute("target");
		for (unsigned i = 0, n = animation.numElements(); i != n; ++i) {
			MPlug target = animation.elementByPhysicalIndex(i).child(target_attr);
			std::string value = target.asString().asChar();
			for (size_t p = 0; p != prefixes.size(); ++p) {
				if (value.length() > prefixes[p].length() && value.compare(0, prefixes[p].length(), prefixes[p]) == 0) {
					target.setString(value.substr(prefixes[p].length()).c_str());
					break;
				}
			}
		}
	}
	g_new_sources.clear();
}

// File import restores the time range when it's done (-importTimeRange keep),
// so a new range is applied afterwards.
static MCallbackId g_range_cb = 0;
static MString g_range_command;

static void apply_range(void*)
{
	MMessage::removeCallback(g_range_cb);
	g_range_cb = 0;
	MGlobal::executeCommand(g_range_command);
}

static void set_range_after_import(double start, double end)
{
	g_range_command = "playbackOptions -ast ";
	g_range_command += start;
	g_range_command += " -min ";
	g_range_command += start;
	g_range_command += " -aet ";
	g_range_command += end;
	g_range_command += " -max ";
	g_range_command += end;
	if (g_range_cb == 0)
		g_range_cb = MSceneMessage::addCallback(MSceneMessage::kAfterImport, apply_range);
}

void maya_import_tools::uninitialize()
{
	if (g_after_import_cb != 0) {
		MMessage::removeCallback(g_after_import_cb);
		g_after_import_cb = 0;
	}
	g_new_sources.clear();
	if (g_range_cb != 0) {
		MMessage::removeCallback(g_range_cb);
		g_range_cb = 0;
	}
}

// keys for every frame with linear tangents, minus the inner keys of flat runs
static MStatus add_linear_keys(MFnAnimCurve& curve_fn, const MTimeArray& times, const MDoubleArray& values)
{
	MTimeArray kept_times;
	MDoubleArray kept_values;
	for (unsigned i = 0, n = values.length(); i != n; ++i) {
		if (i != 0 && i + 1 != n && values[i] == values[i - 1] && values[i] == values[i + 1])
			continue;
		kept_times.append(times[i]);
		kept_values.append(values[i]);
	}
	return curve_fn.addKeys(&kept_times, &kept_values,
			MFnAnimCurve::kTangentLinear, MFnAnimCurve::kTangentLinear);
}

// The motion goes onto a locator, and a camera follows it through a parent
// constraint. Conversion is the inverse of maya_export_tools::export_anm().
MStatus maya_import_tools::import_obj_motion(const xr_obj_motion* omotion, const MString& default_name)
{
	MStatus status;
	MString name = maya_node_name(omotion->name().empty() ? default_name.asChar() : omotion->name());

	MStringArray result;
	if (!(status = MGlobal::executeCommand("spaceLocator -name " + quoted(name + "_locator"), result))) {
		MGlobal::displayError("xray_re: can't create locator");
		return status;
	}
	MString locator = result[0];
	if (!(status = MGlobal::executeCommand("camera", result))) {
		MGlobal::displayError("xray_re: can't create camera");
		return status;
	}
	MString camera;
	MGlobal::executeCommand("rename " + quoted(result[0]) + " " + quoted(name + "_camera"), camera);

	MSelectionList locator_list;
	MObject locator_obj;
	locator_list.add(locator);
	locator_list.getDependNode(0, locator_obj);
	MFnDependencyNode locator_fn(locator_obj);

	double fps = omotion->fps() > 0 ? omotion->fps() : 30.0;
	MTimeArray times;
	MDoubleArray values[6];
	MEulerRotation prev_rot;
	// the last frame is sampled too: SDK motions keep a key there
	for (int32_t frame = omotion->frame_start(), frame_end = omotion->frame_end();
			frame <= frame_end; ++frame) {
		double time = frame/fps;
		fvector3 t, r;
		omotion->evaluate(float(time), t, r);

		times.append(MTime(time, MTime::kSeconds));
		values[0].append(MDistance(t.x, MDistance::kMeters).asCentimeters());
		values[1].append(MDistance(t.y, MDistance::kMeters).asCentimeters());
		values[2].append(MDistance(-t.z, MDistance::kMeters).asCentimeters());

		MEulerRotation rot(-r.x, -r.y, r.z, MEulerRotation::kZXY);
		rot.reorderIt(MEulerRotation::kXYZ);
		if (frame != omotion->frame_start())
			rot.setToClosestSolution(prev_rot);
		prev_rot = rot;
		values[3].append(rot.x);
		values[4].append(rot.y);
		values[5].append(rot.z);
	}

	const xr_envelope* const* envelopes = omotion->envelopes();
	// envelopes: 0-2 translation, 3 yaw (rotateY), 4 pitch (rotateX), 5 bank (rotateZ)
	static const unsigned k_envelope_index[6] = { 0, 1, 2, 4, 3, 5 };
	for (unsigned i = 0; i != 6; ++i) {
		MFnAnimCurve curve_fn;
		curve_fn.create(locator_fn.findPlug(k_plug_names[i], true),
				i >= 3 ? MFnAnimCurve::kAnimCurveTA : MFnAnimCurve::kAnimCurveTL, 0, &status);
		if (!status) {
			MGlobal::displayError("xray_re: can't create animation curve");
			return status;
		}
		const xr_envelope* envelope = envelopes[k_envelope_index[i]];
		curve_fn.setPreInfinityType(maya_infinity(envelope->pre_behaviour()));
		curve_fn.setPostInfinityType(maya_infinity(envelope->post_behaviour()));
		status = add_linear_keys(curve_fn, times, values[i]);
		CHECK_MSTATUS(status);
	}

	if (!(status = MGlobal::executeCommand("parentConstraint -weight 1 " + quoted(locator) +
			" " + quoted(camera)))) {
		MGlobal::displayError("xray_re: can't constrain the camera to the locator");
		return status;
	}

	MTime::Unit unit = MTime::uiUnit();
	set_range_after_import(MTime(omotion->frame_start()/fps, MTime::kSeconds).as(unit),
			MTime(omotion->frame_end()/fps, MTime::kSeconds).as(unit));
	return MS::kSuccess;
}

// Motions become Time Editor animation sources named after the motions;
// clips are left to the user.
// X-Ray motion parameters are kept as dynamic attributes of the animation
// source, so that the OMF export can write them back.
static MPlug xray_attr(MFnDependencyNode& node_fn, const char* name, MFnNumericData::Type type)
{
	MObject attr = node_fn.attribute(name);
	if (attr.isNull()) {
		MFnNumericAttribute attr_fn;
		attr = attr_fn.create(name, name, type, 0);
		attr_fn.setKeyable(false);
		node_fn.addAttribute(attr);
	}
	return node_fn.findPlug(attr, true);
}

static MPlug xray_string_attr(MFnDependencyNode& node_fn, const char* name)
{
	MObject attr = node_fn.attribute(name);
	if (attr.isNull()) {
		MFnTypedAttribute attr_fn;
		attr = attr_fn.create(name, name, MFnData::kString);
		node_fn.addAttribute(attr);
	}
	return node_fn.findPlug(attr, true);
}

// the SDK's motion flags (ESMFlags), as boolean attributes
static const char* const k_motion_flag_attrs[8] = {
	"xrayFx", "xrayStopAtEnd", "xrayNoMix", "xraySyncPart",
	"xrayUseFootSteps", "xrayMoveXForm", "xrayIdle", "xrayUseWeaponBone",
};

// The bone part (cycle) and the start bone (FX) are kept by name: their
// indices depend on the bone order of the model.
void maya_import_tools::store_motion_params(MFnDependencyNode& source_fn, const xr_skl_motion* smotion,
		int order, const xr_object* owner)
{
	xray_attr(source_fn, "xrayOrder", MFnNumericData::kInt).setInt(order);
	xray_attr(source_fn, "xrayLength", MFnNumericData::kInt).setInt(
			smotion->frame_end() - smotion->frame_start());
	unsigned flags = smotion->motion_flags();
	for (unsigned i = 0; i != 8; ++i)
		xray_attr(source_fn, k_motion_flag_attrs[i], MFnNumericData::kBoolean).setBool((flags & (1u << i)) != 0);
	if (flags & ~0xffu)
		xray_attr(source_fn, "xrayOtherFlags", MFnNumericData::kInt).setInt(int(flags & ~0xffu));

	uint16_t bone_or_part = smotion->bone_or_part();
	std::string part, start_bone;
	if (owner && bone_or_part != ALL_PARTITIONS) {
		if (flags & xr_skl_motion::SMF_FX) {
			if (bone_or_part < owner->bones().size() && owner->bones()[bone_or_part])
				start_bone = owner->bones()[bone_or_part]->name();
		} else if (bone_or_part < owner->partitions().size()) {
			part = owner->partitions()[bone_or_part]->name();
		}
	}
	xray_string_attr(source_fn, "xrayBonePart").setString(part.c_str());
	xray_string_attr(source_fn, "xrayStartBone").setString(start_bone.c_str());

	xray_attr(source_fn, "xraySpeed", MFnNumericData::kFloat).setFloat(smotion->speed());
	xray_attr(source_fn, "xrayPower", MFnNumericData::kFloat).setFloat(smotion->power());
	xray_attr(source_fn, "xrayAccrue", MFnNumericData::kFloat).setFloat(smotion->accrue());
	xray_attr(source_fn, "xrayFalloff", MFnNumericData::kFloat).setFloat(smotion->falloff());
}

// Motions of different skeletons - a weapon and the hands holding it - share
// names ("idle", "draw"), and Maya node names are unique, so a second "idle"
// used to come out as "idle1". The sources of a skeleton go to a namespace
// named after its root joint instead ("wpn_body:idle"), the way character sets
// kept them apart in Trax. A namespace can't be named like an existing node -
// Maya quietly takes the next free name then - so it's "character" or "wpn"
// for the usual roots and "<root>_motions" for others, not the joint's own
// name. The motion's own name is kept in "xrayMotionName"
// and the skeleton in "xraySkeleton", a connection from the root joint, which
// is what the OMF export goes by.
// The usual roots get short names of their own: bip01 is the hands (or a
// character), wpn_body a weapon. A second copy of such a skeleton, whose root
// Maya has renamed to "wpn_body1", goes to "wpn1".
static MString skeleton_namespace(const MObject& root_obj)
{
	static const char* const k_known[][2] = {
		{ "bip01", "character" },
		{ "wpn_body", "wpn" },
	};
	std::string name = MFnDependencyNode(root_obj).name().asChar();
	std::string::size_type colon = name.rfind(':');
	if (colon != std::string::npos)
		name.erase(0, colon + 1);
	std::string lower(name);
	for (std::string::iterator it = lower.begin(), end = lower.end(); it != end; ++it)
		*it = char(tolower(uint8_t(*it)));
	for (size_t i = 0; i != sizeof(k_known)/sizeof(k_known[0]); ++i) {
		std::string base(k_known[i][0]);
		if (lower.compare(0, base.length(), base) != 0)
			continue;
		std::string rest = lower.substr(base.length());
		bool digits = true;
		for (std::string::iterator it = rest.begin(), end = rest.end(); it != end; ++it)
			digits = digits && isdigit(uint8_t(*it));
		if (digits)
			return MString(k_known[i][1]) + rest.c_str();
	}
	return maya_node_name(name + "_motions");
}

static void link_source_to_skeleton(MFnDependencyNode& source_fn, const MObject& root_obj)
{
	MObject attr = source_fn.attribute("xraySkeleton");
	if (attr.isNull()) {
		MFnMessageAttribute attr_fn;
		attr = attr_fn.create("xraySkeleton", "xraySkeleton");
		source_fn.addAttribute(attr);
	}
	MDGModifier dg_modifier;
	dg_modifier.connect(MFnDependencyNode(root_obj).findPlug("message", true),
			source_fn.findPlug(attr, true));
	dg_modifier.doIt();
}

// Motion marks are keyable booleans "xrayMark_<name>" on the skeleton root:
// a stepped key of 1 opens an interval, a key of 0 closes it. Their curves
// go into the motion's anim source along with the bones.
void maya_import_tools::create_mark_curves(const xr_skl_motion* smotion, const MObject& root_obj,
		std::vector<std::pair<MPlug, MPlug> >& saved, MSelectionList& members)
{
	if (smotion->marks().empty() || root_obj.isNull())
		return;
	MFnDependencyNode root_fn(root_obj);
	const double start = smotion->frame_start()/smotion->fps();
	for (xr_motion_marks_vec_cit it = smotion->marks().begin(), end = smotion->marks().end(); it != end; ++it) {
		MString attr_name = "xrayMark_" + maya_node_name((*it)->name());
		MObject attr = root_fn.attribute(attr_name);
		if (attr.isNull()) {
			MFnNumericAttribute attr_fn;
			attr = attr_fn.create(attr_name, attr_name, MFnNumericData::kBoolean, 0);
			attr_fn.setKeyable(true);
			attr_fn.setNiceNameOverride(MString("X-Ray mark ") + (*it)->name().c_str());
			root_fn.addAttribute(attr);
		}
		MPlug plug = root_fn.findPlug(attr, true);
		MPlugArray sources;
		if (plug.connectedTo(sources, true, false) && sources.length() != 0) {
			saved.push_back(std::make_pair(sources[0], plug));
			MDGModifier dg_modifier;
			dg_modifier.disconnect(sources[0], plug);
			dg_modifier.doIt();
		}

		// interval ends are inclusive in the engine; times are from the motion start
		std::map<double, double> keys;
		keys[start] = 0;
		for (xr_motion_marks::const_iterator it1 = (*it)->begin(), end1 = (*it)->end(); it1 != end1; ++it1) {
			keys[start + it1->t0] = 1;
			if (it1->t1 > it1->t0)
				keys[start + it1->t1] = 0;
		}
		MTimeArray times;
		MDoubleArray values;
		for (std::map<double, double>::const_iterator it1 = keys.begin(), end1 = keys.end(); it1 != end1; ++it1) {
			times.append(MTime(it1->first, MTime::kSeconds));
			values.append(it1->second);
		}
		MFnAnimCurve curve_fn;
		curve_fn.create(plug, MFnAnimCurve::kAnimCurveTU, 0);
		curve_fn.addKeys(&times, &values, MFnAnimCurve::kTangentStep, MFnAnimCurve::kTangentStep);
		members.add(root_obj);
	}
}

// Motion references (comma separated OMF names) go to the skeleton root for
// the skeletal object export; edited by xray_re_motion_refs.mel.
void maya_import_tools::store_motion_refs(const std::string& refs)
{
	if (m_joints.empty())
		return;
	MDagPath root;
	MDagPath::getAPathTo(m_joints.begin()->second, root);
	for (MDagPath parent(root); parent.pop() && parent.length() > 0 && parent.hasFn(MFn::kJoint);)
		root = parent;
	MFnDependencyNode root_fn(root.node());
	xray_string_attr(root_fn, "xrayMotionRefs").setString(refs.c_str());
	msg("xray_re: motion references: %s", refs.c_str());
	MGlobal::displayInfo(MString("xray_re: motion references: ") + refs.c_str());
}

// Bone parts go to the skeleton root as "part<TAB>bone bone ..." lines.
void maya_import_tools::store_bone_parts(const xr_partition_vec& partitions)
{
	if (partitions.empty() || m_joints.empty())
		return;
	MDagPath root;
	MDagPath::getAPathTo(m_joints.begin()->second, root);
	for (MDagPath parent(root); parent.pop() && parent.length() > 0 && parent.hasFn(MFn::kJoint);)
		root = parent;

	std::string parts;
	for (xr_partition_vec_cit it = partitions.begin(), end = partitions.end(); it != end; ++it) {
		if (!parts.empty())
			parts += '\n';
		parts += (*it)->name();
		parts += '\t';
		const std::vector<std::string>& bones = const_cast<xr_partition*>(*it)->bones();
		for (std::vector<std::string>::const_iterator it1 = bones.begin(), end1 = bones.end(); it1 != end1; ++it1) {
			if (it1 != bones.begin())
				parts += ' ';
			parts += *it1;
		}
	}
	MFnDependencyNode root_fn(root.node());
	xray_string_attr(root_fn, "xrayBoneParts").setString(parts.c_str());
}

MStatus maya_import_tools::import_motions_te(const xr_skl_motion_vec& motions, const xr_object* owner)
{
	MStatus status;

	// The anim sources are built from curves connected to the joints, so free
	// the attributes for a while (they may be driven by clips already).
	std::vector<std::pair<MPlug, MPlug> > saved;
	MDGModifier dg_modifier;
	for (maya_object_map_it it = m_joints.begin(), end = m_joints.end(); it != end; ++it) {
		MFnDependencyNode joint_fn(it->second);
		for (unsigned i = 0; i != 6; ++i) {
			MPlug plug = joint_fn.findPlug(k_plug_names[i], true);
			MPlugArray sources;
			if (plug.connectedTo(sources, true, false) && sources.length() != 0) {
				saved.push_back(std::make_pair(sources[0], plug));
				dg_modifier.disconnect(sources[0], plug);
			}
		}
	}
	dg_modifier.doIt();

	MSelectionList saved_selection;
	MGlobal::getActiveSelectionList(saved_selection);

	// marks live on the skeleton root
	MObject root_obj;
	if (!m_joints.empty()) {
		MDagPath root;
		MDagPath::getAPathTo(m_joints.begin()->second, root);
		for (MDagPath parent(root); parent.pop() && parent.length() > 0 && parent.hasFn(MFn::kJoint);)
			root = parent;
		root_obj = root.node();
	}
	MString ns;
	if (!root_obj.isNull()) {
		ns = skeleton_namespace(root_obj);
		if (!MNamespace::namespaceExists(":" + ns)) {
			MString parent(":");
			MNamespace::addNamespace(ns, &parent);
		}
		// a node of that name blocks it; the motions stay out of namespaces
		// rather than land in whatever Maya made of the name
		if (!MNamespace::namespaceExists(":" + ns)) {
			msg("xray_re: can't create namespace %s", ns.asChar());
			ns.clear();
		}
	}

	start_progress(motions.size(), "Importing motions");
	for (xr_skl_motion_vec_cit it = motions.begin(), end = motions.end(); it != end; ++it) {
		const xr_skl_motion* smotion = *it;
		MSelectionList members;
		MTimeArray times[6];
		MDoubleArray values[6];
		for (xr_bone_motion_vec_cit it1 = smotion->bone_motions().begin(),
				end1 = smotion->bone_motions().end(); it1 != end1; ++it1) {
			const xr_bone_motion* bmotion = *it1;
			maya_object_map_it joint_it = find_joint(bmotion, size_t(it1 - smotion->bone_motions().begin()));
			if (joint_it == m_joints.end())
				continue;
			MFnIkJoint joint_fn(joint_it->second);
			sample_bone_motion(smotion, bmotion, joint_fn, times, values);

			const xr_envelope* const* envelopes = bmotion->envelopes();
			for (unsigned i = 0; i != 6; ++i) {
				MFnAnimCurve curve_fn;
				curve_fn.create(joint_fn.findPlug(k_plug_names[i], true),
						i >= 3 ? MFnAnimCurve::kAnimCurveTA : MFnAnimCurve::kAnimCurveTL,
						0, &status);
				CHECK_MSTATUS(status);
				curve_fn.setPreInfinityType(maya_infinity(envelopes[i]->pre_behaviour()));
				curve_fn.setPostInfinityType(maya_infinity(envelopes[i]->post_behaviour()));
				status = curve_fn.addKeys(&times[i], &values[i],
						MFnAnimCurve::kTangentStep, MFnAnimCurve::kTangentStep);
				CHECK_MSTATUS(status);
				times[i].clear();
				values[i].clear();
			}
			members.add(joint_it->second);
		}
		if (members.isEmpty())
			continue;
		create_mark_curves(smotion, root_obj, saved, members);

		// move the curves from the joints into an animation source
		MString name = maya_node_name(smotion->name()), source;
		MGlobal::setActiveSelectionList(members);
		status = MGlobal::executeCommand("timeEditorAnimSource -aso -type animCurveTL"
				" -type animCurveTA -type animCurveTU -rsa 1 " + quoted(name), source);
		if (!status) {
			msg("xray_re: can't create animation source for %s", smotion->name().c_str());
			MGlobal::displayError(MString("xray_re: can't create animation source for ") +
				smotion->name().c_str());
			break;
		}
		// the command appends "_AnimSource" to the name; the skeleton's
		// namespace goes in front of it
		MGlobal::executeCommand("rename " + quoted(source) + " " +
				quoted(ns.length() ? ":" + ns + ":" + name : name), source);
		MSelectionList source_list;
		MObject source_obj;
		if (source_list.add(source) && source_list.getDependNode(0, source_obj)) {
			g_new_sources.push_back(MObjectHandle(source_obj));
			if (g_after_import_cb == 0)
				g_after_import_cb = MSceneMessage::addCallback(MSceneMessage::kAfterImport, fix_source_targets);
			MFnDependencyNode source_fn(source_obj);
			store_motion_params(source_fn, smotion, int(it - motions.begin()), owner);
			xray_string_attr(source_fn, "xrayMotionName").setString(smotion->name().c_str());
			if (!root_obj.isNull())
				link_source_to_skeleton(source_fn, root_obj);
		}
		advance_progress();
	}
	end_progress();
	MGlobal::setActiveSelectionList(saved_selection);

	for (std::vector<std::pair<MPlug, MPlug> >::iterator it = saved.begin(), end = saved.end();
			it != end; ++it) {
		dg_modifier.connect(it->first, it->second);
	}
	dg_modifier.doIt();

	return status;
}

MStatus maya_import_tools::import_motions(const xr_skl_motion_vec& motions, MObject& character_obj,
		const xr_object* owner)
{
	MStatus status = MS::kFailure;

	// bones with no names go by their place in the skin
	bool nameless = false;
	for (xr_skl_motion_vec_cit it = motions.begin(), end = motions.end(); it != end && !nameless; ++it) {
		for (xr_bone_motion_vec_cit it1 = (*it)->bone_motions().begin(),
				end1 = (*it)->bone_motions().end(); it1 != end1 && !nameless; ++it1)
			nameless = (*it1)->name().empty();
	}
	m_bone_order.clear();
	if (nameless) {
		collect_bone_order();
		msg("xray_re: motions have no bone names, matching %u bones by order",
				unsigned(m_bone_order.size()));
		MGlobal::displayInfo(MString("xray_re: motions have no bone names, matching bones by their order in the skin"));
		size_t num_bones = motions.front()->bone_motions().size();
		if (!m_bone_order.empty() && num_bones != m_bone_order.size()) {
			msg("xray_re: the motions are for %u bones, the skin has %u",
					unsigned(num_bones), unsigned(m_bone_order.size()));
			char text[128];
			sprintf(text, "xray_re: the motions are for %u bones, the skin has %u: probably another skeleton",
					unsigned(num_bones), unsigned(m_bone_order.size()));
			MGlobal::displayWarning(text);
		}
	}

	// check the skeleton once instead of complaining for every motion
	std::set<std::string> missing;
	size_t num_found = 0;
	for (xr_skl_motion_vec_cit it = motions.begin(), end = motions.end(); it != end; ++it) {
		for (xr_bone_motion_vec_cit it1 = (*it)->bone_motions().begin(),
				end1 = (*it)->bone_motions().end(); it1 != end1; ++it1) {
			size_t index = size_t(it1 - (*it)->bone_motions().begin());
			if (find_joint(*it1, index) != m_joints.end()) {
				++num_found;
			} else if ((*it1)->name().empty()) {
				char text[32];
				sprintf(text, "#%u", unsigned(index));
				missing.insert(text);
			} else {
				missing.insert((*it1)->name());
			}
		}
	}
	if (nameless && m_bone_order.empty()) {
		msg("xray_re: motions have no bone names and the skeleton has no skin to take the bone order from");
		MGlobal::displayError("xray_re: motions have no bone names; select a joint of a skinned skeleton");
		return MS::kFailure;
	}
	if (!motions.empty() && num_found == 0) {
		msg("xray_re: motions don't match the selected character (no common bones)");
		MGlobal::displayError("xray_re: motions don't match the selected character (no common bones)");
		return MS::kFailure;
	}
	for (std::set<std::string>::const_iterator it = missing.begin(), end = missing.end(); it != end; ++it) {
		msg("xray_re: can't find bone %s referenced by motions", it->c_str());
		MGlobal::displayWarning(MString("xray_re: can't find bone ") + it->c_str() + " referenced by motions");
	}

	if (character_obj.isNull())
		return import_motions_te(motions, owner);

	start_progress(motions.size(), "Importing motions");
	for (xr_skl_motion_vec_cit it = motions.begin(), end = motions.end();
			it != end; ++it) {
		if (!(status = import_motion(*it, character_obj)))
			break;
		advance_progress();
	}
	end_progress();

	return status;
}

void maya_import_tools::set_default_options(void)
{
	m_target_sdk = xray_re::SDK_VER_0_4;
	m_use_joint_orient = false;
}

MStatus maya_import_tools::parse_options(const MString& options)
{
	MStatus status;
	MStringArray params;

	if (!(status = options.split(';', params)))
		return status;

	for (size_t i = 0; i < params.length(); i++) {
		MStringArray key_value;
		if (!(status = params[i].split('=', key_value)))
			return status;

		if (key_value.length() < 2)
			continue;

		if (key_value[0] == "sdk_ver") {
			xray_re::sdk_version ver = xray_re::sdk_version_from_string(key_value[1].asChar());
			m_target_sdk = (ver == xray_re::SDK_VER_UNKNOWN ? xray_re::SDK_VER_0_4 : ver);
		}
		else if (key_value[0] == "use_joint_orient") {
			m_use_joint_orient = (key_value[1] == "true");
		}
	}

	return MS::kSuccess;
}
