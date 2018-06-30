/*
	liblightmodbus - a lightweight, multiplatform Modbus library
	Copyright (C) 2017 Jacek Wieczorek <mrjjot@gmail.com>

	This file is part of liblightmodbus.

	Liblightmodbus is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Liblightmodbus is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <string.h>
#include <lightmodbus/lightmodbus.h>
#include <lightmodbus/slave.h>
#include <lightmodbus/parser.h>
#include <lightmodbus/slave/sregs.h>
#include <lightmodbus/slave/scoils.h>

ModbusError modbusBuildException( ModbusSlave *status, uint8_t function, ModbusExceptionCode code )
{
	//Generates modbus exception frame in allocated memory frame
	//Returns generated frame length

	//Check if given pointer is valid
	if ( status == NULL || code == 0 ) return MODBUS_ERROR_OTHER;

	//Setup 'last exception' in slave struct
	status->lastException = code;

	#ifndef LIGHTMODBUS_STATIC_MEM_SLAVE_RESPONSE
		//Reallocate frame memory
		status->response.frame = (uint8_t *) calloc( 5, sizeof( uint8_t ) );
		if ( status->response.frame == NULL ) return MODBUS_ERROR_ALLOC;
	#else
		if ( 5 * sizeof( uint8_t ) > LIGHTMODBUS_STATIC_MEM_SLAVE_RESPONSE ) return MODBUS_ERROR_ALLOC;
	#endif

	ModbusParser *exception = (ModbusParser *) status->response.frame;

	//Setup exception frame
	exception->exception.address = status->address;
	exception->exception.function = ( 1 << 7 ) | function;
	exception->exception.code = code;
	exception->exception.crc = modbusCRC( exception->frame, 3 );

	//Set frame length - frame is ready
	status->response.length = 5;

	//So, user should rather know, that master slave had to throw exception, right?
	//That's the reason exception should be thrown - just like that, an information
	return MODBUS_ERROR_EXCEPTION;
}

ModbusError modbusParseRequest( ModbusSlave *status )
{
	//Parse and interpret given modbus frame on slave-side
	uint8_t err = 0;

	//Check if given pointer is valid
	if ( status == NULL ) return MODBUS_ERROR_OTHER;

	//Reset response frame status
	status->response.length = 0;

	//If there is memory allocated for response frame - free it
	#ifndef LIGHTMODBUS_STATIC_MEM_SLAVE_RESPONSE
		free( status->response.frame );
		status->response.frame = NULL;
	#endif

	//If user tries to parse an empty frame return error
	//That enables us to ommit the check in each parsing function
	if ( status->request.length < 4u || status->request.frame == NULL ) return MODBUS_ERROR_OTHER;

	//Check CRC
	//The CRC of the frame is copied to a variable in order to avoid an unaligned memory access,
	//which can cause runtime errors in some platforms like AVR and ARM.
	uint16_t crc;

	memcpy(&crc, status->request.frame + status->request.length - 2, 2);

	if ( crc != modbusCRC( status->request.frame, status->request.length - 2 ) )
		return MODBUS_ERROR_CRC;


	ModbusParser *parser = (ModbusParser *) status->request.frame;

	//If frame is not broadcasted and address doesn't match skip parsing
	if ( parser->base.address != status->address && parser->base.address != 0 )
		return MODBUS_ERROR_OK;

	//Firstly, check user function array
	#ifdef LIGHTMODBUS_USER_FUNCTIONS
	if ( status->userFunctions != NULL && status->userFunctionCount != 0 )
	{
		uint16_t i;
		for ( i = 0; i < status->userFunctionCount; i++ )
		{
			if ( status->userFunctions[i].function == parser->base.function )
			{
				//If the function is overriden and handler pointer is valid, user the callback
				if ( status->userFunctions[i].handler != NULL ) 
					err = status->userFunctions[i].handler( status, parser );
				else
					err = MODBUS_ERROR_BAD_FUNCTION; //Function overriden, but pointer is invalid

				//Search till first match
				break;
			}
		}
	}
	else //User did not override any function
	#endif
	{
		switch ( parser->base.function )
		{
			#if defined(LIGHTMODBUS_F01S) || defined(LIGHTMODBUS_F02S)
				case 1: //Read multiple coils
				case 2: //Read multiple discrete inputs
					err = modbusParseRequest0102( status, parser );
					break;
			#endif

			#if defined(LIGHTMODBUS_F03S) || defined(LIGHTMODBUS_F04S)
				case 3: //Read multiple holding registers
				case 4: //Read multiple input registers
					err = modbusParseRequest0304( status, parser );
					break;
			#endif

			#ifdef LIGHTMODBUS_F05S
				case 5: //Write single coil
					err = modbusParseRequest05( status, parser );
					break;
			#endif

			#ifdef LIGHTMODBUS_F06S
				case 6: //Write single holding reg
					err = modbusParseRequest06( status, parser );
					break;
			#endif

			#ifdef LIGHTMODBUS_F15S
				case 15: //Write multiple coils
					err = modbusParseRequest15( status, parser );
					break;
			#endif

			#ifdef LIGHTMODBUS_F16S
				case 16: //Write multiple holding registers
					err = modbusParseRequest16( status, parser );
					break;
			#endif

			#ifdef LIGHTMODBUS_F22S
				case 22: //Mask write single register
					err = modbusParseRequest22( status, parser );
					break;
			#endif

			default:
				err = MODBUS_ERROR_BAD_FUNCTION;
				break;
		}
	}

	//If function is unknown, return MODBUS_ERROR_EXCEPTION or anything returned by modbusBuildException
	if ( err == MODBUS_ERROR_BAD_FUNCTION )
		if ( parser->base.address != 0 ) err = modbusBuildException( status, parser->base.function, MODBUS_EXCEP_ILLEGAL_FUNCTION );

	return err;
}

ModbusError modbusSlaveInit( ModbusSlave *status )
{
	//Very basic init of slave side
	//User has to modify pointers etc. himself

	//Check if given pointer is valid
	if ( status == NULL ) return MODBUS_ERROR_OTHER;

	//Reset response frame status
	#ifndef LIGHTMODBUS_STATIC_MEM_SLAVE_REQUEST
		status->request.frame = NULL;
	#else
		memset( status->request.frame, 0, LIGHTMODBUS_STATIC_MEM_SLAVE_REQUEST );
	#endif
	status->request.length = 0;

	#ifndef LIGHTMODBUS_STATIC_MEM_SLAVE_RESPONSE
		status->response.frame = NULL;
	#else
		memset( status->response.frame, 0, LIGHTMODBUS_STATIC_MEM_SLAVE_RESPONSE );
	#endif
	status->response.length = 0;

	if ( status->address == 0 )
	{
		status->address = 1;
		return MODBUS_ERROR_OTHER;
	}

	//Some safety checks
	if ( status->registerCount == 0 || 
		#ifdef LIGHTMODBUS_REGISTER_CALLBACK
			status->registerCallback == NULL
		#else
			status->registers == NULL
		#endif
		)
	{
		status->registerCount = 0;
		#ifdef LIGHTMODBUS_REGISTER_CALLBACK
			status->registerCallback = NULL;
		#else
			status->registers = NULL;
		#endif
	}

	if ( status->coilCount == 0 || status->coils == NULL )
	{
		status->coilCount = 0;
		status->coils = NULL;
	}

	if ( status->discreteInputCount == 0 || status->discreteInputs == NULL )
	{
		status->discreteInputCount = 0;
		status->discreteInputs = NULL;
	}

	if ( status->inputRegisterCount == 0 ||
		#ifdef LIGHTMODBUS_REGISTER_CALLBACK
			status->registerCallback == NULL
		#else
			status->inputRegisters == NULL
		#endif
		)
	{
		status->inputRegisterCount = 0;
		#ifdef LIGHTMODBUS_REGISTER_CALLBACK
			status->registerCallback = NULL;
		#else
			status->inputRegisters = NULL;
		#endif
	}

	return MODBUS_ERROR_OK;
}

ModbusError modbusSlaveEnd( ModbusSlave *status )
{
	//Check if given pointer is valid
	if ( status == NULL ) return MODBUS_ERROR_OTHER;

	//Free memory
	#ifndef LIGHTMODBUS_STATIC_MEM_SLAVE_RESPONSE
		free( status->response.frame );
		status->response.frame = NULL;
	#endif

	return MODBUS_ERROR_OK;
}
