#pragma once

struct BindlessTable
{
	nvrhi::BindingLayoutHandle m_Layout;
	nvrhi::DescriptorTableHandle m_DescriptorTable;

	BindlessTable(nvrhi::DeviceHandle device, nvrhi::BindlessLayoutDesc desc, bool resizeToMaxCapacity)
	{
		m_Layout = device->createBindlessLayout(desc);
		
		{
			m_DescriptorTable = device->createDescriptorTable(m_Layout);

			auto bindlessDesc = m_Layout->getBindlessDesc();
			if (resizeToMaxCapacity && m_DescriptorTable->getCapacity() < bindlessDesc->maxCapacity)
				device->resizeDescriptorTable(m_DescriptorTable, bindlessDesc->maxCapacity);
		}
	}
};