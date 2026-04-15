#pragma once

#include "CalcTangents.h"
#include "Core/Mesh.h"

namespace Util
{
    CalcTangents::CalcTangents() {
        iface.m_getNumFaces = get_num_faces;
        iface.m_getNumVerticesOfFace = get_num_vertices_of_face;

        iface.m_getNormal = get_normal;
        iface.m_getPosition = get_position;
        iface.m_getTexCoord = get_tex_coords;
        iface.m_setTSpaceBasic = set_tspace_basic;

        context.m_pInterface = &iface;
    }

    void CalcTangents::calc(Mesh* mesh) {
        context.m_pUserData = mesh;
        genTangSpaceDefault(&this->context);
    }

    int CalcTangents::get_num_faces(const SMikkTSpaceContext* context) {
        return static_cast<Mesh*> (context->m_pUserData)->triangleData.count;
    }

    int CalcTangents::get_num_vertices_of_face([[ maybe_unused ]] const SMikkTSpaceContext* context, 
        [[ maybe_unused ]] const int iFace) {
            return 3;
    }

    void CalcTangents::get_position(const SMikkTSpaceContext* context,
        float* outpos,
        const int iFace, const int iVert) {

        Mesh* working_mesh = static_cast<Mesh*> (context->m_pUserData);

        auto index = get_vertex_index(context, iFace, iVert);
        auto vertex = working_mesh->vertexData.vertices[index];

        outpos[0] = vertex.Position.x;
        outpos[1] = vertex.Position.y;
        outpos[2] = vertex.Position.z;
    }

    void CalcTangents::get_normal(const SMikkTSpaceContext* context,
        float* outnormal,
        const int iFace, const int iVert) {
        Mesh* working_mesh = static_cast<Mesh*> (context->m_pUserData);

        auto index = get_vertex_index(context, iFace, iVert);
        auto vertex = working_mesh->vertexData.vertices[index];


        outnormal[0] = vertex.Normal.x;
        outnormal[1] = vertex.Normal.y;
        outnormal[2] = vertex.Normal.z;
    }

    void CalcTangents::get_tex_coords(const SMikkTSpaceContext* context,
        float* outuv,
        const int iFace, const int iVert) {
        Mesh* working_mesh = static_cast<Mesh*> (context->m_pUserData);

        auto index = get_vertex_index(context, iFace, iVert);
        auto vertex = working_mesh->vertexData.vertices[index];

        outuv[0] = vertex.Texcoord0.x;
        outuv[1] = vertex.Texcoord0.y;
    }

    void CalcTangents::set_tspace_basic(const SMikkTSpaceContext* context,
        const float* tangentu,
        const float fSign, const int iFace, const int iVert) {
        Mesh* working_mesh = static_cast<Mesh*> (context->m_pUserData);


        auto index = get_vertex_index(context, iFace, iVert);
        auto* vertex = &working_mesh->vertexData.vertices[index];

        vertex->Tangent.x = tangentu[0];
        vertex->Tangent.y = tangentu[1];
        vertex->Tangent.z = tangentu[2];
        vertex->Handedness = fSign;
    }

    int CalcTangents::get_vertex_index(const SMikkTSpaceContext* context, int iFace, int iVert) {
        Mesh* working_mesh = static_cast<Mesh*> (context->m_pUserData);

        auto& triangle = working_mesh->triangleData.triangles[iFace];

        int index;

        if (iVert == 0)
            index = triangle.x;
        else if (iVert == 1)
            index = triangle.y;
        else
            index = triangle.z;

        return index;
    }
}