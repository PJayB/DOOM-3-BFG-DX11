#pragma once

class idLayoutManager 
{
public:
    static void Init();
    static void Shutdown();

    template<class VertexType> static ID3D11InputLayout* GetLayout()
    {
        return idVertexLayout<VertexType>::s_layout;
    }

private:
    template<class VertexType> class idVertexLayout 
    {
        static ID3D11InputLayout* s_layout;
    };
};
