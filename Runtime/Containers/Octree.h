#pragma once
#include <cassert>
#include <memory>
#include <functional>
#include <concepts>
#include <type_traits>
#include <iterator>
#include <algorithm>
#include "Core/Defines.h"
#include "Math/Math.h"
#include "Memory/Memory.h"
#include "Containers/Concepts.h"
#include "Containers/Map.h"
#include "Containers/Vector.h"
#include "RHI/DebugContext.h"
#include "Math/Bounds.h"

namespace Sailor
{
	template<typename TElementType, typename TAllocator = Memory::DefaultGlobalAllocator>
	class TOctree
	{
		static constexpr uint32_t NumElementsInNode = 8u;

	protected:

		struct TBounds
		{
			TBounds() = default;
			TBounds(const glm::ivec3& pos, const glm::ivec3& extents) : m_position(pos), m_extents(extents) {}

			bool operator==(const TBounds& rhs) const { return m_position == rhs.m_position || m_extents == rhs.m_extents; }

			glm::ivec3 m_position{};
			glm::ivec3 m_extents{};
		};

		struct TNode
		{
			// Bottom   Top
			// |0|1|    |4|5|
			// |2|3|    |6|7|
			__forceinline constexpr uint32_t GetIndex(int32_t x, int32_t y, int32_t z) const { return (z < 0 ? 0 : 1) + (x < 0 ? 1 : 0) * 2 + (y < 0 ? 0 : 1) * 4; }

			__forceinline bool IsLeaf() const { return m_internal == nullptr; }
			__forceinline bool Contains(const glm::ivec3& pos, const glm::ivec3& extents) const
			{
				const int32_t halfSize = m_size / 2;
				return m_center.x - halfSize < pos.x - extents.x &&
					m_center.y - halfSize < pos.y - extents.y &&
					m_center.z - halfSize < pos.z - extents.z &&
					m_center.x + halfSize > pos.x + extents.x &&
					m_center.y + halfSize > pos.y + extents.y &&
					m_center.z + halfSize > pos.z + extents.z;
			}

			__forceinline bool Overlaps(const glm::ivec3& pos, const glm::ivec3& extents) const
			{
				const int32_t halfSize = m_size / 2;
				return
					(m_center.x - halfSize < pos.x + extents.x) && (m_center.x + halfSize > pos.x - extents.x) &&
					(m_center.y - halfSize < pos.y + extents.y) && (m_center.y + halfSize > pos.y - extents.y) &&
					(m_center.z - halfSize < pos.z + extents.z) && (m_center.z + halfSize > pos.z - extents.z);
			}

			__forceinline bool CanCollapse() const
			{
				if (IsLeaf())
				{
					return false;
				}

				for (uint32_t i = 0; i < 8; i++)
				{
					if (!m_internal[i].IsLeaf() || m_internal[i].m_elements.Num() > 0)
					{
						return false;
					}
				}

				return true;
			}

			__forceinline void Insert(const TElementType& element, const glm::ivec3& pos, const glm::ivec3& extents)
			{
				m_elements[element] = TBounds(pos, extents);
			}

			__forceinline bool Remove(const TElementType& element)
			{
				return m_elements.Remove(element);
			}

			uint32_t m_size = 1;
			glm::ivec3 m_center{};
			TNode* m_internal = nullptr;
			TMap<TElementType, TBounds> m_elements{ NumElementsInNode };
		};

	public:

		// Constructors & Destructor
		TOctree(glm::ivec3 center = glm::ivec3(0, 0, 0), uint32_t size = 16536u, uint32_t minSize = 4) : m_map(size)
		{
			m_minSize = minSize;
			m_root = Memory::New<TNode>(m_allocator);
			m_root->m_size = size;
			m_root->m_center = center;
		}

		virtual ~TOctree()
		{
			Clear();

			Memory::Delete<TNode>(m_allocator, m_root);
			m_root = nullptr;
		}

		TOctree(const TOctree& octree) : TOctree(octree.m_center, octree.m_root->m_size, octree.m_minSize)
		{
			m_num = octree.m_num;
			CopyFrom_Internal(*m_root, *octree.m_root);
		}

		TOctree& operator= (const TOctree& octree)
		{
			Clear();

			m_num = octree.m_num;
			m_minSize = octree.m_minSize;

			CopyFrom_Internal(*m_root, *octree.m_root);

			return *this;
		}

		TOctree(TOctree&& octree)
		{
			Swap(this, octree);
		}

		TOctree& operator= (TOctree&& octree)
		{
			Swap(this, octree);
			return *this;
		}

		static void Swap(TOctree&& lhs, TOctree&& rhs)
		{
			std::swap(rhs->m_root, lhs->m_root);
			std::swap(rhs->m_allocator, lhs->m_allocator);
			std::swap(rhs->m_num, lhs->m_num);
			std::swap(rhs->m_minSize, lhs->m_minSize);
			std::swap(rhs->m_map, lhs->m_map);
		}

		void Clear()
		{
			m_num = 0;
			if (m_root)
			{
				Clear_Internal(*m_root);
			}
			m_map.Clear();
		}

		bool Contains(const TElementType& element) const { return m_root && m_map.ContainsKey(element); }
		size_t Num() const { return m_num; }
		size_t NumNodes() const { return m_numNodes; }

		//TVector<TElementType, TAllocator> GetElements(glm::ivec3 extents) const { return TVector(); }

		bool Insert(const glm::ivec3& pos, const glm::ivec3& extents, const TElementType& element)
		{
			if (Insert_Internal(*m_root, pos, extents, element))
			{
				m_num++;
				return true;
			}
			return false;
		}

		bool Update(const glm::ivec3& pos, const glm::ivec3& extents, const TElementType& element)
		{
			TNode** node;
			if (m_map.Find(element, node))
			{
				// If we're still in the octant then we just update the bounds
				if ((*node)->Contains(pos, extents))
				{
					(*node)->m_elements.UpdateKey(element) = TBounds(pos, extents);
					return true;
				}

				// If not then remove & reinsert
				if ((*node)->Remove(element))
				{
					Resolve_Internal(**node);
				}

				if (Insert_Internal(*m_root, pos, extents, element))
				{
					m_num++;
					return true;
				}

				// Cannot insert the element into octree
				m_map.Remove(element);
			}

			if (Insert_Internal(*m_root, pos, extents, element))
			{
				m_num++;
				return true;
			}

			return false;
		}

		bool Remove(const TElementType& element)
		{
			TNode** node;
			if (m_map.Find(element, node))
			{
				if ((*node)->Remove(element))
				{
					Resolve_Internal(**node);
					m_map.Remove(element);
					m_num--;

					return true;
				}
			}
			return false;
		}

		__forceinline void Resolve() { Resolve_Internal(*m_root); }
		__forceinline void DrawOctree(RHI::DebugContext& context, float duration = 0.0f) const { DrawOctree_Internal(*m_root, context, duration); }
		__forceinline void Trace(const Math::Frustum& frustum, TVector<TElementType>& outElements) const
		{
			outElements.Clear(false);

			if (frustum.OverlapsAABB(Math::AABB(m_root->m_center, (float)m_root->m_size * glm::vec3(0.5f, 0.5f, 0.5f))))
			{
				Trace_Internal(*m_root, frustum, outElements);
			}
		}

	protected:

		void Trace_Internal(const TNode& node, const Math::Frustum& frustum, TVector<TElementType>& outElements) const
		{
			if (node.m_elements.Num())
			{
				for (const auto& el : node.m_elements)
				{
					if (frustum.OverlapsAABB(Math::AABB(el.m_second->m_position, el.m_second->m_extents)))
					{
						outElements.Add(el.m_first);
					}
				}
			}

			if (!node.IsLeaf())
			{
				for (uint32_t i = 0; i < 8; i++)
				{
					if (frustum.OverlapsAABB(Math::AABB(node.m_internal[i].m_center, (float)node.m_internal[i].m_size * glm::vec3(0.5f, 0.5f, 0.5f))))
					{
						Trace_Internal(node.m_internal[i], frustum, outElements);
					}
				}
			}
		}

		void GetElementsInChildren(const TNode& node, TVector<TElementType>& outElements) const
		{
			for (auto& el : node.m_elements)
			{
				outElements.Add(el.m_first);
			}

			if (node.IsLeaf())
			{
				return;
			}

			for (uint32_t i = 0; i < 8; i++)
			{
				GetElementsInChildren(node.m_internal[i], outElements);
			}
		}

		void DrawOctree_Internal(const TNode& node, RHI::DebugContext& context, float duration = 0.0f) const
		{
			if (!node.IsLeaf())
			{
				for (uint32_t i = 0; i < 8; i++)
				{
					DrawOctree_Internal(node.m_internal[i], context, duration);
				}
			}

			glm::vec4 color = glm::vec4(0.2f, 1.0f, 0.2f, 1.0f);

			if (node.IsLeaf() && node.m_elements.Num() == 0)
			{
				color = glm::vec4(1.0f, 0.2f, 0.2f, 1.0f);
			}

			Math::AABB aabb;
			aabb.m_min = glm::vec3(node.m_center) - (float)node.m_size * glm::vec3(0.5f, 0.5f, 0.5f);
			aabb.m_max = glm::vec3(node.m_center) + (float)node.m_size * glm::vec3(0.5f, 0.5f, 0.5f);

			context.DrawAABB(aabb, color, duration);

			for (auto& el : node.m_elements)
			{
				Math::AABB aabb;
				aabb.m_min = glm::vec3(el.m_second.m_position - el.m_second.m_extents);
				aabb.m_max = glm::vec3(el.m_second.m_position + el.m_second.m_extents);

				const auto color = glm::vec4((0x4 & reinterpret_cast<size_t>(&node)) / 16.0f,
					(0x4 & reinterpret_cast<size_t>(&node) >> 4) / 16.0f,
					(0x4 & reinterpret_cast<size_t>(&node) >> 8) / 16.0f,
					(0x4 & reinterpret_cast<size_t>(&node) >> 12) / 16.0f);

				context.DrawAABB(aabb, color, duration);
			}
		}

		void Resolve_Internal(TNode& node)
		{
			if (node.m_elements.Num())
			{
				return;
			}

			if (!node.IsLeaf())
			{
				for (uint32_t i = 0; i < 8; i++)
				{
					Resolve_Internal(node.m_internal[i]);
				}
			}

			if (node.CanCollapse())
			{
				Collapse(node);
			}
		}

		void Clear_Internal(TNode& node)
		{
			node.m_elements.Clear();

			if (node.IsLeaf())
			{
				return;
			}

			for (uint32_t i = 0; i < 8; i++)
			{
				Clear_Internal(node.m_internal[i]);
			}

			Collapse(node);
		}

		void CopyFrom_Internal(TNode& node, const TNode& rhsNode)
		{
			node.m_size = rhsNode.m_size;
			node.m_center = rhsNode.m_center;
			node.m_elements = rhsNode.m_elements;

			if (rhsNode.m_internal != nullptr)
			{
				Subdivide(node);

				for (uint32_t i = 0; i < 8; i++)
				{
					CopyFrom_Internal(node.m_internal[i], rhsNode.m_internal[i]);
				}
			}
		}

		bool Insert_Internal(TNode& node, const glm::ivec3& pos, const glm::ivec3& extents, const TElementType& element)
		{
			const bool bIsLeaf = node.IsLeaf();
			const bool bContains = node.Contains(pos, extents);

			if (!bContains)
			{
				return false;
			}

			if (bIsLeaf)
			{
				node.Insert(element, pos, extents);

				if (node.m_elements.Num() == NumElementsInNode && node.m_size > m_minSize)
				{
					auto elements = std::move(node.m_elements);
					node.m_elements.Clear();

					Subdivide(node);

					for (const auto& el : elements)
					{
						//Check all was placed;
						const bool bInserted = Insert_Internal(node, el.m_second->m_position, el.m_second->m_extents, el.m_first);

						if (!bInserted) continue;
						check(bInserted);
					}

					return true;
				}

				m_map[element] = &node;

				return true;
			}
			else
			{
				const glm::ivec3 delta = pos - node.m_center;
				auto innerNodeIndex = node.GetIndex(delta.x, delta.y, delta.z);
				if (Insert_Internal(node.m_internal[innerNodeIndex], pos, extents, element))
				{
					return true;
				}

				node.Insert(element, pos, extents);
				m_map[element] = &node;
			}

			return true;
		}

		__forceinline void Subdivide(TNode& node)
		{
			check(node.IsLeaf());

			// Bottom   Top
			// |0|1|    |4|5|
			// |2|3|    |6|7|
			const glm::ivec3 offset[] = { glm::ivec3(1, -1, -1), glm::ivec3(1, -1, 1), glm::ivec3(-1, -1, -1), glm::ivec3(-1, -1, 1),
										  glm::ivec3(1, 1, -1), glm::ivec3(1, 1, 1), glm::ivec3(-1, 1, -1), glm::ivec3(-1, 1, 1) };

			const int32_t quarterSize = node.m_size / 4;
			node.m_internal = static_cast<TNode*>(m_allocator.Allocate(sizeof(TNode) * 8));

			for (uint32_t i = 0; i < 8; i++)
			{
				new (node.m_internal + i) TNode();
				node.m_internal[i].m_size = quarterSize * 2;
				node.m_internal[i].m_center = offset[i] * quarterSize + node.m_center;
			}

			m_numNodes += 8;
		}

		__forceinline void Collapse(TNode& node)
		{
			for (uint32_t i = 0; i < 8; i++)
			{
				node.m_internal[i].~TNode();
			}

			m_allocator.Free(node.m_internal);
			node.m_internal = nullptr;
			m_numNodes -= 8;
		}

		TNode* m_root{};
		size_t m_num = 0u;
		uint32_t m_minSize = 1;
		size_t m_numNodes = 1u;
		TAllocator m_allocator{};
		TMap<TElementType, TNode*> m_map{};
	};

	SAILOR_API void RunOctreeBenchmark();
}