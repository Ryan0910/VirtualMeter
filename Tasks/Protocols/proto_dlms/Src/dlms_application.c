/**
 * @brief		
 * @details		
 * @date		azenk@2019-01-10
 **/

/* Includes ------------------------------------------------------------------*/
#include "stdbool.h"
#include "string.h"
#include "system.h"
#include "dlms_types.h"
#include "dlms_application.h"
#include "dlms_association.h"
#include "dlms_lexicon.h"
#include "cosem_objects.h"
#include "axdr.h"
#include "mids.h"
#include "mbedtls/gcm.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"
#include "mbedtls/bignum.h"

/* Private define ------------------------------------------------------------*/
#define DLMS_REQ_LIST_MAX   ((uint8_t)6) //一次请求可接受的最大数据项数

/* Private typedef -----------------------------------------------------------*/
/**	
  * @brief 
  */
enum __appl_result
{
    APPL_SUCCESS = 0,
    APPL_DENIED,
    APPL_NOMEM,
    APPL_BLOCK_MISS,
    APPL_OBJ_NODEF,
    APPL_OBJ_MISS,
    APPL_OBJ_OVERFLOW,
    APPL_UNSUPPORT,
    APPL_ENC_FAILD,
    APPL_OTHERS,
};

/**	
  * @brief 解析报文
  */
struct __appl_request
{
    uint8_t service;//enum __dlms_request_type
	
	union
	{
		struct
		{
			uint8_t *transaction;//签名报文标识
			uint8_t *originator;//签名报文发起者
			uint8_t *recipient;//签名报文接收者
			uint8_t *date;//签名报文时间
			uint8_t *others;//签名报文其它信息
			uint8_t *content;//数据
			uint8_t *signature;//签名
		} sign;
		
		struct
		{
			uint8_t *transaction;//签名报文标识
			uint8_t *originator;//签名报文发起者
			uint8_t *recipient;//签名报文接收者
			uint8_t *date;//签名报文时间
			uint8_t *others;//签名报文其它信息
			uint8_t *key;//签名报文时间
			uint8_t *content;//数据
		} cipher;
		
		struct
		{
			uint8_t control;//控制码
			uint16_t number;//块计数
			uint16_t ack;//块计数确认
			uint8_t *content;//数据
		} block;
		
		struct
		{
			uint8_t *originator;//加密报文发起者
			uint8_t *content;//数据
		} global;
		
	} general;
	
	uint8_t sc;//加密方式
	uint8_t *plain;//明文的首地址
    uint8_t type;//enum __dlms_get_request_type || enum __dlms_set_request_type || enum __dlms_action_request_type
    uint8_t id;//Invoke-Id-And-Priority
    struct
    {
        uint8_t *classid;//类标识
        uint8_t *obis;//数据项标识
        uint8_t *index;//属性、方法标识
        uint8_t *data;//get时为访问选项，set和action 时为携带数据
        uint8_t *end;//多包传输结束
        uint32_t block;//数据包数
        uint16_t length;//数据长度
        uint8_t active;//当前请求是否激活
        
    } info[DLMS_REQ_LIST_MAX];
};

struct __cosem_instance
{
    TypeObject Object; //数据对象
    ObjectPara Para; //对象参数
    ObjectErrs Errs; //调用结果
    uint8_t Right; //对象访问权限
};

/**	
  * @brief 数据对象
  */
struct __cosem_request
{
    uint8_t Actived; //激活的条目数量
    uint32_t Block; //块计数
    struct __cosem_instance Entry[DLMS_REQ_LIST_MAX]; //总条目
};

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/**	
  * @brief 指向当前正在访问的数据对象
  */
static struct __cosem_request *Current = (struct __cosem_request *)0;

/**	
  * @brief 指向当前正在访问的数据标识
  */
static uint8_t *instance_name = (uint8_t *)0;

/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/
/**
  * @brief 格式化请求数据
  * 
  */
static uint8_t *request_formatter(uint32_t mid, const uint8_t *in, uint16_t size, uint16_t *out)
{
    static uint8_t result[9];
    
    struct __meta_identifier id;
    enum __axdr_type type;
    union __axdr_container container;
    int64_t val = 0;
    uint64_t *pval = (uint64_t *)&val;
    
	if(!M_UISID(mid))
    {
        *out = size;
        return((uint8_t *)in);
    }
    
    if(!AXDR_CONTAINABLE(in[0]))
    {
        *out = size;
        return((uint8_t *)in);
    }
    
    axdr.decode(in, &type, &container);
    
    switch(type)
    {
        case AXDR_BOOLEAN:
        case AXDR_ENUM:
        case AXDR_UNSIGNED:
        {
            val = (int64_t)container.vu8;
            break;
        }
        case AXDR_INTEGER:
        {
            val = (int64_t)container.vi8;
            break;
        }
        case AXDR_LONG:
        {
            val = (int64_t)container.vi16;
            break;
        }
        case AXDR_LONG_UNSIGNED:
        {
            val = (int64_t)container.vu16;
            break;
        }
        case AXDR_DOUBLE_LONG:
        {
            val = (int64_t)container.vi32;
            break;
        }
        case AXDR_DOUBLE_LONG_UNSIGNED:
        {
            val = (int64_t)container.vu32;
            break;
        }
        case AXDR_FLOAT32:
        {
            val = (int64_t)container.vf32;
            break;
        }
        case AXDR_FLOAT64:
        {
            val = (int64_t)container.vf64;
            break;
        }
        case AXDR_LONG64:
        {
            val = (int64_t)container.vi64;
            break;
        }
        case AXDR_LONG64_UNSIGNED:
        {
            val = (int64_t)container.vu64;
            break;
        }
    }
    
    M_U2ID(mid, id);
    
    M_SCALING(val, ~id.scale);
    
    result[0] = AXDR_LONG64;
    result[1] = (*pval >> 56) & 0xff;
    result[2] = (*pval >> 48) & 0xff;
    result[3] = (*pval >> 40) & 0xff;
    result[4] = (*pval >> 32) & 0xff;
    result[5] = (*pval >> 24) & 0xff;
    result[6] = (*pval >> 16) & 0xff;
    result[7] = (*pval >> 8) & 0xff;
    result[8] = (*pval >> 0) & 0xff;
    
    *out = 9;
    return(result);
}

/**
  * @brief 格式化返回数据
  * 
  */
static uint16_t response_formatter(uint32_t mid, const uint8_t *in, uint16_t size, uint8_t *out)
{
    struct __meta_identifier id;
    uint64_t val = 0;
    
	if(!M_UISID(mid))
    {
        return(heap.copy(out, in, size));
    }
    
    if(in[0] != AXDR_LONG64)
    {
        return(heap.copy(out, in, size));
    }
    
    axdr.decode(in, 0, &val);
    
    M_U2ID(mid, id);
    
    M_SCALING(val, id.scale);
    
    switch((enum __axdr_type)id.type)
    {
        case AXDR_BOOLEAN:
        case AXDR_ENUM:
        case AXDR_INTEGER:
        case AXDR_UNSIGNED:
        {
            out[0] = id.type;
            out[1] = (val >> 0) & 0xff;
            return(2);
        }
        case AXDR_LONG:
        case AXDR_LONG_UNSIGNED:
        {
            out[0] = id.type;
            out[1] = (val >> 8) & 0xff;
            out[2] = (val >> 0) & 0xff;
            return(3);
        }
        case AXDR_DOUBLE_LONG:
        case AXDR_DOUBLE_LONG_UNSIGNED:
        {
            out[0] = id.type;
            out[1] = (val >> 24) & 0xff;
            out[2] = (val >> 16) & 0xff;
            out[3] = (val >> 8) & 0xff;
            out[4] = (val >> 0) & 0xff;
            return(5);
        }
        case AXDR_FLOAT32:
        {
            float conv = 0;
            uint32_t *pconv = (uint32_t *)&conv;
            conv = val;
            
            out[0] = id.type;
            out[1] = (*pconv >> 24) & 0xff;
            out[2] = (*pconv >> 16) & 0xff;
            out[3] = (*pconv >> 8) & 0xff;
            out[4] = (*pconv >> 0) & 0xff;
            return(5);
        }
        case AXDR_FLOAT64:
        {
            double conv = 0;
            uint64_t *pconv = (uint64_t *)&conv;
            conv = val;
            
            out[0] = id.type;
            out[1] = (*pconv >> 56) & 0xff;
            out[2] = (*pconv >> 48) & 0xff;
            out[3] = (*pconv >> 40) & 0xff;
            out[4] = (*pconv >> 32) & 0xff;
            out[5] = (*pconv >> 24) & 0xff;
            out[6] = (*pconv >> 16) & 0xff;
            out[7] = (*pconv >> 8) & 0xff;
            out[8] = (*pconv >> 0) & 0xff;
            return(9);
        }
        case AXDR_LONG64:
        case AXDR_LONG64_UNSIGNED:
        {
            out[0] = id.type;
            out[1] = (val >> 56) & 0xff;
            out[2] = (val >> 48) & 0xff;
            out[3] = (val >> 40) & 0xff;
            out[4] = (val >> 32) & 0xff;
            out[5] = (val >> 24) & 0xff;
            out[6] = (val >> 16) & 0xff;
            out[7] = (val >> 8) & 0xff;
            out[8] = (val >> 0) & 0xff;
            return(9);
        }
        default:
        {
            return(heap.copy(out, in, size));
        }
    }
}

/**
  * @brief 索引数据报文中有效数据
  * 
  */
static enum __appl_result parse_dlms_frame(const uint8_t *info, \
                                           uint16_t length, \
                                           struct __appl_request *request, \
										   bool origin)
{
	uint8_t service;
    uint16_t frame_length = 0;
    uint16_t frame_decoded = 0;
    uint16_t info_length = 0;
    uint16_t ninfo;
    uint8_t nloop;
    
    if(!request || !info || !length)
    {
        return(APPL_OTHERS);
    }
    
	service = info[0];
	if(origin)
	{
		request->service = info[0];
	}
    frame_decoded = 1;
    
    switch(service)
    {
		case GNL_SIGN_REQUEST:
		{
			uint8_t title[8];
			uint8_t cspubkey[96];
			uint8_t len_cspubkey;
			uint8_t z[48];
			unsigned char hash[48];
			mbedtls_ecdsa_context ctx;
			mbedtls_mpi r, s;
			uint8_t len_signature;
			uint8_t *ctext;
			uint16_t len_ctext;
            int ret = 0;
            
			//TransactionId
			if(*(info + frame_decoded) != 0x08)
			{
				return(APPL_ENC_FAILD);
			}
			else
			{
				request->general.sign.transaction = (uint8_t *)(info + frame_decoded);
				frame_decoded += 9;
			}
			//OriginatorSystemTitle
			if(*(info + frame_decoded) != 0x08)
			{
				return(APPL_ENC_FAILD);
			}
			else
			{
				dlms_asso_callingtitle(title);
				if(memcmp(title, (info + frame_decoded + 1), 8) != 0)
				{
					return(APPL_ENC_FAILD);
				}
				request->general.sign.originator = (uint8_t *)(info + frame_decoded);
				frame_decoded += 9;
			}
			//RecipientSystemTitle
			if(*(info + frame_decoded) != 0x08)
			{
				return(APPL_ENC_FAILD);
			}
			else
			{
				dlms_asso_localtitle(title);
				if(memcmp(title, (info + frame_decoded + 1), 8) != 0)
				{
					return(APPL_ENC_FAILD);
				}
				request->general.sign.recipient = (uint8_t *)(info + frame_decoded);
				frame_decoded += 9;
			}
			//DateTime
			if(*(info + frame_decoded) == 0x00)
			{
				request->general.sign.date = (uint8_t *)(info + frame_decoded);
				frame_decoded += 1;
			}
			else if(*(info + frame_decoded) == 0x0c)
			{
				request->general.sign.date = (uint8_t *)(info + frame_decoded);
				frame_decoded += 0x0c;
			}
			else
			{
				return(APPL_ENC_FAILD);
			}
			//OtherInformation
			if(*(info + frame_decoded) == 0x00)
			{
				request->general.sign.others = (uint8_t *)(info + frame_decoded);
				frame_decoded += 1;
			}
			else
			{
				request->general.sign.others = (uint8_t *)(info + frame_decoded);
				frame_decoded += axdr.length.decode((info + frame_decoded), &frame_length);
				frame_decoded += frame_length;
				if(frame_length >= 128)
				{
					return(APPL_ENC_FAILD);
				}
			}
			
			//Data
            frame_decoded += axdr.length.decode((info + frame_decoded), &frame_length);
			request->general.sign.content = (uint8_t *)(info + frame_decoded);
            if(frame_length < 3)
			{
				return(APPL_ENC_FAILD);
			}
			
			request->general.sign.signature = (uint8_t *)(info + frame_decoded + frame_length + 1);
            if((length == (frame_decoded + frame_length + 1 + 64)) && (*(info + frame_decoded + frame_length) == 64))
            {
				len_signature = 64;
            }
			else if((length == (frame_length + frame_decoded + 1 + 96)) && (*(info + frame_decoded + frame_length) == 96))
			{
				len_signature = 96;
			}
			else
			{
				return(APPL_ENC_FAILD);
			}
			
			len_cspubkey = dlms_asso_cspubkey(cspubkey);
			
			if((len_cspubkey != 64) && (len_cspubkey != 96))
			{
				return(APPL_ENC_FAILD);
			}
			memset(z, 0, sizeof(z));
			z[len_cspubkey / 2 - 1] = 1;
			
			mbedtls_mpi_init( &r );
			mbedtls_mpi_init( &s );
			mbedtls_ecp_keypair_init( &ctx );
			if(len_cspubkey == 64)
			{
				mbedtls_ecp_group_load( &ctx.grp, MBEDTLS_ECP_DP_SECP256R1 );
			}
			else
			{
				mbedtls_ecp_group_load( &ctx.grp, MBEDTLS_ECP_DP_SECP384R1 );
			}
			
			if((ret = mbedtls_mpi_read_binary( &ctx.Q.X, &cspubkey[0], len_cspubkey / 2)) != 0)
			{
				goto cleanup;
			}
			if((ret = mbedtls_mpi_read_binary( &ctx.Q.Y, &cspubkey[len_cspubkey / 2], len_cspubkey / 2)) != 0)
			{
				goto cleanup;
			}
			if((ret = mbedtls_mpi_read_binary( &ctx.Q.Z, z, len_cspubkey / 2)) != 0)
			{
				goto cleanup;
			}
			
			if((ret = mbedtls_mpi_read_binary(&r, &request->general.sign.signature[1], len_signature / 2)) != 0)
			{
				goto cleanup;
			}
			if((ret = mbedtls_mpi_read_binary(&s, &request->general.sign.signature[1 + len_signature / 2], len_signature / 2)) != 0)
			{
				goto cleanup;
			}
			
			len_ctext = (3 + 3 * 8) + (1 + request->general.sign.date[0]) + (1 + request->general.sign.others[0]) + frame_length;
			ctext = heap.dalloc(len_ctext);
			if(!ctext)
			{
				ret = MBEDTLS_ERR_MPI_ALLOC_FAILED;
				goto cleanup;
			}
			
			memcpy(&ctext[0], request->general.sign.transaction, 9);
			memcpy(&ctext[9], request->general.sign.originator, 9);
			memcpy(&ctext[18], request->general.sign.recipient, 9);
			memcpy(&ctext[27], request->general.sign.date, 1 + request->general.sign.date[0]);
			memcpy(&ctext[27 + 1 + request->general.sign.date[0]], request->general.sign.others, 1 + request->general.sign.others[0]);
			memcpy(&ctext[27 + 1 + request->general.sign.date[0] + 1 + request->general.sign.others[0]], request->general.sign.content, frame_length);
			
			if(len_cspubkey == 64)
			{
				if( ( ret = mbedtls_sha256_ret( ctext, len_ctext, hash, 0 ) ) != 0 )
				{
					heap.free(ctext);
					goto cleanup;
				}
				heap.free(ctext);
				
				if( ( ret = mbedtls_ecdsa_verify( &ctx.grp, hash, 32, \
													 &ctx.Q, &r, &s) ) != 0 )
				{
					goto cleanup;
				}
			}
			else
			{
				if( ( ret = mbedtls_sha512_ret( ctext, len_ctext, hash, 1 ) ) != 0 )
				{
					heap.free(ctext);
					goto cleanup;
				}
				heap.free(ctext);
				
				if( ( ret = mbedtls_ecdsa_verify( &ctx.grp, hash, 48, \
													 &ctx.Q, &r, &s) ) != 0 )
				{
					goto cleanup;
				}
			}
			
	cleanup:
			mbedtls_mpi_free( &r );
			mbedtls_mpi_free( &s );
			mbedtls_ecdsa_free( &ctx );
			if(ret != 0)
			{
				return(APPL_ENC_FAILD);
			}
			
			return(parse_dlms_frame(request->general.sign.content, frame_length, request, false));
			
            break;
		}
		case GNL_GLO_CIPHER_REQUEST:
		case GNL_DED_CIPHER_REQUEST:
        {
            uint8_t *add = (void *)0;
            uint8_t *input = (void *)0;
            uint8_t iv[12];
            uint8_t ekey[32];
            uint8_t akey[32];
            uint8_t len_ekey;
            uint8_t len_akey;
            mbedtls_gcm_context ctx;
            int ret = 0;
            
			if(*(info + frame_decoded) != 0x08)
			{
				return(APPL_OTHERS);
			}
			else
			{
				memcpy(iv, (info + frame_decoded + 1), 8);
				frame_decoded += 9;
			}
			
            frame_decoded += axdr.length.decode((info + frame_decoded), &frame_length);
            request->sc = info[frame_decoded];
            
            if(length != (frame_length + frame_decoded))
            {
                return(APPL_OTHERS);
            }
            
            if(request->service == GNL_DED_CIPHER_REQUEST)
            {
                len_ekey = dlms_asso_dedkey(ekey);
            }
            else
            {
                len_ekey = dlms_asso_ekey(ekey);
            }
            
            len_akey = dlms_asso_akey(akey);
            
            heap.copy(&iv[8], &info[frame_decoded + 1], 4);
            
            mbedtls_gcm_init(&ctx);
            ret = mbedtls_gcm_setkey(&ctx,
                                     MBEDTLS_CIPHER_ID_AES,
                                     ekey,
                                     len_ekey*8);
            
            switch(request->sc & 0xf0)
            {
                case 0x10:
                {
                    //仅认证
                    if(ret)
                    {
                        break;
                    }
                    
                    if(frame_length < (5 + 12))
                    {
                        ret = -1;
                        break;
                    }
                    
                    add = heap.dalloc(frame_length + 1 + len_akey);
                    if(!add)
                    {
                        ret = -1;
                        break;
                    }
                    
                    add[0] = request->sc;
                    heap.copy(&add[1], akey, len_akey);
                    heap.copy(&add[1+len_akey], &info[frame_decoded + 5], (frame_length - 5 - 12));
                    
                    if(ret)
                    {
                        break;
                    }
                    
                    ret = mbedtls_gcm_auth_decrypt(&ctx,
                                                   0,
                                                   iv,
                                                   12,
                                                   add,
                                                   (1 + len_akey + frame_length - 5 - 12),
                                                   &info[frame_decoded + (frame_length - 12 + 1)],
                                                   12,
                                                   (unsigned char *)0,
                                                   (unsigned char *)0);
                    info_length = frame_length - 5 - 12;
                    
                    break;
                }
                case 0x20:
                {
                    //仅加密
                    if(ret)
                    {
                        break;
                    }
                    
                    if(frame_length < 5)
                    {
                        ret = -1;
                        break;
                    }
                    
                    input = heap.dalloc(frame_length - 5);
                    if(!input)
                    {
                        ret = -1;
                        break;
                    }
                    
                    heap.copy(input, &info[frame_decoded + 5], (frame_length - 5));
                    
                    ret = mbedtls_gcm_auth_decrypt(&ctx,
                                                   (frame_length - 5),
                                                   iv,
                                                   12,
                                                   (unsigned char *)0,
                                                   0,
                                                   (unsigned char *)0,
                                                   0,
                                                   input,
                                                   (unsigned char *)&info[frame_decoded + 5]);
                    info_length = frame_length - 5;
                    
                    break;
                }
                case 0x30:
                {
                    //加密加认证
                    if(ret)
                    {
                        break;
                    }
                    
                    if(frame_length < (5 + 12))
                    {
                        ret = -1;
                        break;
                    }
                    
                    add = heap.dalloc(1 + len_akey);
                    if(!add)
                    {
                        ret = -1;
                        break;
                    }
                    
                    add[0] = request->sc;
                    heap.copy(&add[1], akey, len_akey);
                    
                    input = heap.dalloc(frame_length - 5 - 12);
                    if(!input)
                    {
                        ret = -1;
                        break;
                    }
                    
                    heap.copy(input, &info[frame_decoded + 5], (frame_length - 5 - 12));
                    
                    ret = mbedtls_gcm_auth_decrypt(&ctx,
                                                   (frame_length - 5 - 12),
                                                   iv,
                                                   12,
                                                   add,
                                                   (1 + len_akey),
                                                   &info[1 + frame_length - 12 + 1],
                                                   12,
                                                   input,
                                                   (unsigned char *)&info[frame_decoded + 5]);
                    info_length = frame_length - 5 - 12;
                    
                    break;
                }
                default:
                {
                    ret = -1;
                }
            }
            
            mbedtls_gcm_free( &ctx );
            
            if(add)
            {
                heap.free(add);
            }
            
            if(input)
            {
                heap.free(input);
            }
            
            if(!ret)
            {
				request->plain = (uint8_t *)&info[frame_decoded + 5];
				return(parse_dlms_frame(request->plain, info_length, request, false));
            }
            else
            {
                return(APPL_ENC_FAILD);
            }
            
            break;
        }
        case GLO_GET_REQUEST:
        case GLO_SET_REQUEST:
        case GLO_ACTION_REQUEST:
        case DED_GET_REQUEST:
        case DED_SET_REQUEST:
        case DED_ACTION_REQUEST:
        {
            uint8_t *add = (void *)0;
            uint8_t *input = (void *)0;
            uint8_t iv[12];
            uint8_t ekey[32];
            uint8_t akey[32];
            uint8_t len_ekey;
            uint8_t len_akey;
            mbedtls_gcm_context ctx;
            int ret = 0;
            
            frame_decoded += axdr.length.decode((info + frame_decoded), &frame_length);
            request->sc = info[frame_decoded];
            
            if(length != (frame_length + frame_decoded))
            {
                return(APPL_OTHERS);
            }
            
            if((request->service == DED_GET_REQUEST) || \
                (request->service == DED_SET_REQUEST) || \
                (request->service == DED_ACTION_REQUEST))
            {
                len_ekey = dlms_asso_dedkey(ekey);
            }
            else
            {
                len_ekey = dlms_asso_ekey(ekey);
            }
            
            len_akey = dlms_asso_akey(akey);
            
            dlms_asso_callingtitle(iv);
            heap.copy(&iv[8], &info[3], 4);
            
            mbedtls_gcm_init(&ctx);
            ret = mbedtls_gcm_setkey(&ctx,
                                     MBEDTLS_CIPHER_ID_AES,
                                     ekey,
                                     len_ekey*8);
            
            switch(request->sc & 0xf0)
            {
                case 0x10:
                {
                    //仅认证
                    if(ret)
                    {
                        break;
                    }
                    
                    if(frame_length < (5 + 12))
                    {
                        ret = -1;
                        break;
                    }
                    
                    add = heap.dalloc(frame_length + 1 + len_akey);
                    if(!add)
                    {
                        ret = -1;
                        break;
                    }
                    
                    
                    add[0] = request->sc;
                    heap.copy(&add[1], akey, len_akey);
                    heap.copy(&add[1+len_akey], &info[7], (frame_length - 5 - 12));
                    
                    if(ret)
                    {
                        break;
                    }
                    
                    ret = mbedtls_gcm_auth_decrypt(&ctx,
                                                   0,
                                                   iv,
                                                   12,
                                                   add,
                                                   (1 + len_akey + frame_length - 5 - 12),
                                                   &info[frame_decoded + (frame_length - 12 + 1)],
                                                   12,
                                                   (unsigned char *)0,
                                                   (unsigned char *)0);
                    info_length = frame_length - 5 - 12;
                    
                    break;
                }
                case 0x20:
                {
                    //仅加密
                    if(ret)
                    {
                        break;
                    }
                    
                    if(frame_length < 5)
                    {
                        ret = -1;
                        break;
                    }
                    
                    input = heap.dalloc(frame_length - 5);
                    if(!input)
                    {
                        ret = -1;
                        break;
                    }
                    
                    heap.copy(input, &info[7], (frame_length - 5));
                    
                    ret = mbedtls_gcm_auth_decrypt(&ctx,
                                                   (frame_length - 5),
                                                   iv,
                                                   12,
                                                   (unsigned char *)0,
                                                   0,
                                                   (unsigned char *)0,
                                                   0,
                                                   input,
                                                   (unsigned char *)&info[7]);
                    info_length = frame_length - 5;
                    
                    break;
                }
                case 0x30:
                {
                    //加密加认证
                    if(ret)
                    {
                        break;
                    }
                    
                    if(frame_length < (5 + 12))
                    {
                        ret = -1;
                        break;
                    }
                    
                    add = heap.dalloc(1 + len_akey);
                    if(!add)
                    {
                        ret = -1;
                        break;
                    }
                    
                    add[0] = request->sc;
                    heap.copy(&add[1], akey, len_akey);
                    
                    input = heap.dalloc(frame_length - 5 - 12);
                    if(!input)
                    {
                        ret = -1;
                        break;
                    }
                    
                    heap.copy(input, &info[7], (frame_length - 5 - 12));
                    
                    ret = mbedtls_gcm_auth_decrypt(&ctx,
                                                   (frame_length - 5 - 12),
                                                   iv,
                                                   12,
                                                   add,
                                                   (1 + len_akey),
                                                   &info[1 + frame_length - 12 + 1],
                                                   12,
                                                   input,
                                                   (unsigned char *)&info[7]);
                    info_length = frame_length - 5 - 12;
                    
                    break;
                }
                default:
                {
                    ret = -1;
                }
            }
            
            mbedtls_gcm_free( &ctx );
            
            if(add)
            {
                heap.free(add);
            }
            
            if(input)
            {
                heap.free(input);
            }
			
            if(!ret)
            {
				request->plain = (uint8_t *)&info[frame_decoded + 5];
				return(parse_dlms_frame(request->plain, info_length, request, false));
            }
            else
            {
                return(APPL_ENC_FAILD);
            }
            
            break;
        }
        case GET_REQUEST:
        case SET_REQUEST:
        case ACTION_REQUEST:
        {
			if(origin)
			{
				request->plain = (uint8_t *)info;
			}
			
			request->type = request->plain[1];
			request->id = request->plain[2];
			
			switch(request->plain[0])
			{
				case GET_REQUEST:
				{
					switch(request->type)
					{
						case GET_NORMAL:
						{
							request->info[0].active = 0xff;
							request->info[0].classid = &request->plain[3];
							request->info[0].obis = &request->plain[5];
							request->info[0].index = &request->plain[11];
							request->info[0].data = &request->plain[12];
							if(info_length < 13)
							{
								return(APPL_OTHERS);
							}
							else
							{
								request->info[0].length = info_length - 12;
							}
							break;
						}
						case GET_NEXT:
						{
							request->info[0].active = 0xff;
							request->info[0].block = request->plain[3];
							request->info[0].block <<= 8;
							request->info[0].block += request->plain[4];
							request->info[0].block <<= 8;
							request->info[0].block += request->plain[5];
							request->info[0].block <<= 8;
							request->info[0].block += request->plain[6];
							if(info_length < 7)
							{
								return(APPL_OTHERS);
							}
							else
							{
								request->info[0].length = 0;
							}
							break;
						}
						case GET_WITH_LIST:
						{
							if((!request->plain[3]) || (request->plain[3] > DLMS_REQ_LIST_MAX))
							{
								return(APPL_UNSUPPORT);
							}
							
							ninfo = 4;
							
							for(nloop=0; nloop<request->plain[3]; nloop ++)
							{
								request->info[nloop].active = 0xff;
								request->info[nloop].classid = &request->plain[ninfo + 0];
								request->info[nloop].obis = &request->plain[ninfo + 2];
								request->info[nloop].index = &request->plain[ninfo + 8];
								request->info[nloop].data = &request->plain[ninfo + 9];
								
								if(info_length < (ninfo + 9))
								{
									return(APPL_OTHERS);
								}
								
								if(*(request->info[nloop].data) != 0)
								{
									return(APPL_UNSUPPORT);
								}
								
								request->info[nloop].length = 0;
								
								ninfo += 10;
							}
							
							break;
						}
						default:
						{
							return(APPL_UNSUPPORT);
						}
					}
					break;
				}
				case SET_REQUEST:
				{
					switch(request->type)
					{
						case SET_NORMAL:
						{
							request->info[0].active = 0xff;
							request->info[0].classid = &request->plain[3];
							request->info[0].obis = &request->plain[5];
							request->info[0].index = &request->plain[11];
							request->info[0].data = &request->plain[13];
							request->info[0].length = info_length - 13;
							if((info_length < 14) || request->plain[12])
							{
								return(APPL_OTHERS);
							}
							break;
						}
						case SET_FIRST_BLOCK:
						{
							request->info[0].active = 0xff;
							request->info[0].classid = &request->plain[3];
							request->info[0].obis = &request->plain[5];
							request->info[0].index = &request->plain[11];
							request->info[0].end = &request->plain[13];
							request->info[0].block = request->plain[14];
							request->info[0].block <<= 8;
							request->info[0].block += request->plain[15];
							request->info[0].block <<= 8;
							request->info[0].block += request->plain[16];
							request->info[0].block <<= 8;
							request->info[0].block += request->plain[17];
							request->info[0].data = &request->plain[18 + axdr.length.decode(&request->plain[18], &request->info[0].length)];
							if((info_length < 20) || request->plain[12])
							{
								return(APPL_OTHERS);
							}
							break;
						}
						case SET_WITH_BLOCK:
						{
							request->info[0].active = 0xff;
							request->info[0].end = &request->plain[3];
							request->info[0].block = request->plain[4];
							request->info[0].block <<= 8;
							request->info[0].block += request->plain[5];
							request->info[0].block <<= 8;
							request->info[0].block += request->plain[6];
							request->info[0].block <<= 8;
							request->info[0].block += request->plain[7];
							request->info[0].data = &request->plain[8 + axdr.length.decode(&request->plain[8], &request->info[0].length)];
							if(info_length < 10)
							{
								return(APPL_OTHERS);
							}
							break;
						}
						case SET_WITH_LIST:
						case SET_WITH_LIST_AND_FIRST_BLOCK:
						default:
						{
							return(APPL_UNSUPPORT);
						}
					}
					break;
				}
				case ACTION_REQUEST:
				{
					switch(request->type)
					{
						case ACTION_NORMAL:
						{
							request->info[0].active = 0xff;
							request->info[0].classid = &request->plain[3];
							request->info[0].obis = &request->plain[5];
							request->info[0].index = &request->plain[11];
							
							if(request->plain[12])
							{
								request->info[0].data = &request->plain[13];
								request->info[0].length = info_length - 13;
								if(info_length < 14)
								{
									return(APPL_OTHERS);
								}
							}
							else
							{
								request->info[0].data = (uint8_t *)0;
								request->info[0].length = 0;
								if(info_length < 12)
								{
									return(APPL_OTHERS);
								}
							}
							break;
						}
						case ACTION_NEXT_BLOCK:
						{
							request->info[0].active = 0xff;
							request->info[0].end = &request->plain[3];
							request->info[0].block = request->plain[4];
							request->info[0].block <<= 8;
							request->info[0].block += request->plain[5];
							request->info[0].block <<= 8;
							request->info[0].block += request->plain[6];
							request->info[0].block <<= 8;
							request->info[0].block += request->plain[7];
							request->info[0].data = &request->plain[8 + axdr.length.decode(&request->plain[8], &request->info[0].length)];
							if(info_length < 10)
							{
								return(APPL_OTHERS);
							}
							break;
						}
						case ACTION_FIRST_BLOCK:
						{
							request->info[0].active = 0xff;
							request->info[0].classid = &request->plain[3];
							request->info[0].obis = &request->plain[5];
							request->info[0].index = &request->plain[11];
							request->info[0].end = &request->plain[12];
							request->info[0].block = request->plain[13];
							request->info[0].block <<= 8;
							request->info[0].block += request->plain[14];
							request->info[0].block <<= 8;
							request->info[0].block += request->plain[15];
							request->info[0].block <<= 8;
							request->info[0].block += request->plain[16];
							request->info[0].data = &request->plain[17 + axdr.length.decode(&request->plain[17], &request->info[0].length)];
							if(info_length < 19)
							{
								return(APPL_OTHERS);
							}
							break;
						}
						case ACTION_WITH_LIST:
						case ACTION_WITH_LIST_AND_FIRST_BLOCK:
						case ACTION_WITH_BLOCK:
						default:
						{
							return(APPL_UNSUPPORT);
						}
					}
					break;
				}
				default:
				{
					return(APPL_UNSUPPORT);
				}
			}
			
			return(APPL_SUCCESS);
			
            break;
        }
        default:
        {
			return(APPL_OTHERS);
        }
    }
}

/**
  * @brief 生成访问实例
  * 
  */
static enum __appl_result make_cosem_instance(const struct __appl_request *request)
{
    uint8_t cnt = 0;
    struct __cosem_request_desc desc;
    
    //获取当前链接下附属的对象内存
    Current = (struct __cosem_request *)dlms_asso_storage();
    if(!Current)
    {
        //生成当前链接下附属的对象内存
        Current = (struct __cosem_request *)dlms_asso_attach_storage(sizeof(struct __cosem_request));
        
        if(!Current)
        {
            return(APPL_OTHERS);
        }
    }
    
    desc.request = (enum __dlms_request_type)request->service;
    desc.level = dlms_asso_level();
    desc.suit = dlms_asso_suit();
    
    switch(request->service)
    {
        case GET_REQUEST:
        case GLO_GET_REQUEST:
        case DED_GET_REQUEST:
        {
            switch(request->type)
            {
                case GET_NORMAL:
                {
                    heap.set(&Current->Entry[0], 0, sizeof(Current->Entry[0]));
                    
                    desc.descriptor.classid = request->info[0].classid[0];
                    desc.descriptor.classid <<= 8;
                    desc.descriptor.classid += request->info[0].classid[1];
                    heap.copy(desc.descriptor.obis, request->info[0].obis, 6);
                    desc.descriptor.index = request->info[0].index[0];
                    desc.descriptor.selector = 0;
                    dlms_lex_parse(&desc, \
                                   (union __dlms_right *)&Current->Entry[0].Right, \
                                   &Current->Entry[0].Para.Input.OID, \
                                   &Current->Entry[0].Para.Input.MID);
                    
                    Current->Block = 0;//块计数清零
                    Current->Actived = 1;//一个请求条目
                    
                    Current->Entry[0].Para.Input.Buffer = request->info[0].data;
                    Current->Entry[0].Para.Input.Size = request->info[0].length;
                    Current->Entry[0].Object = CosemLoadAttribute(desc.descriptor.classid, desc.descriptor.index, false);
                    
                    break;
                }
                case GET_NEXT:
                {
                    //不支持多实例
                    if((Current->Actived != 1) || (!Current->Entry[0].Object))
                    {
                        return(APPL_OBJ_OVERFLOW);
                    }
                    
                    //块计数验证
                    if((Current->Block + 1) != request->info->block)
                    {
                        return(APPL_BLOCK_MISS);
                    }
                    
                    //更新块计数
                    Current->Block = request->info->block;
                    
                    Current->Entry[0].Para.Input.Buffer = (uint8_t *)0;
                    Current->Entry[0].Para.Input.Size = 0;
                    
                    break;
                }
                case GET_WITH_LIST:
                {
                    Current->Block = 0;//块计数清零
                    Current->Actived = 0;
                    
                    for(cnt=0; cnt<DLMS_REQ_LIST_MAX; cnt++)
                    {
                        if(!request->info[cnt].active)
                        {
                            break;
                        }
                        
                        heap.set(&Current->Entry[cnt], 0, sizeof(Current->Entry[cnt]));
                        
                        desc.descriptor.classid = request->info[cnt].classid[0];
                        desc.descriptor.classid <<= 8;
                        desc.descriptor.classid += request->info[cnt].classid[1];
                        heap.copy(desc.descriptor.obis, request->info[cnt].obis, 6);
                        desc.descriptor.index = request->info[cnt].index[0];
                        desc.descriptor.selector = 0;
                        dlms_lex_parse(&desc, (union __dlms_right *)&Current->Entry[cnt].Right, \
                                       &Current->Entry[cnt].Para.Input.OID, \
                                       &Current->Entry[cnt].Para.Input.MID);
                        
                        Current->Actived += 1;//一个请求条目
                        
                        Current->Entry[cnt].Para.Input.Buffer = request->info[cnt].data;
                        Current->Entry[cnt].Para.Input.Size = request->info[cnt].length;
                        Current->Entry[cnt].Object = CosemLoadAttribute(desc.descriptor.classid, desc.descriptor.index, false);
                    }
                    
                    break;
                }
                default:
                {
                    return(APPL_UNSUPPORT);
                }
            }
            break;
        }
        case SET_REQUEST:
        case GLO_SET_REQUEST:
        case DED_SET_REQUEST:
        {
            switch(request->type)
            {
                case SET_NORMAL:
                {
                    heap.set(&Current->Entry[0], 0, sizeof(Current->Entry[0]));
                    
                    desc.descriptor.classid = request->info[0].classid[0];
                    desc.descriptor.classid <<= 8;
                    desc.descriptor.classid += request->info[0].classid[1];
                    heap.copy(desc.descriptor.obis, request->info[0].obis, 6);
                    desc.descriptor.index = request->info[0].index[0];
                    desc.descriptor.selector = 0;
                    dlms_lex_parse(&desc, (union __dlms_right *)&Current->Entry[0].Right, \
                                   &Current->Entry[0].Para.Input.OID, \
                                   &Current->Entry[0].Para.Input.MID);
                    
                    Current->Block = 0;//块计数清零
                    Current->Actived = 1;//一个请求条目
                    
                    
                    Current->Entry[0].Para.Input.Buffer = request_formatter(Current->Entry[0].Para.Input.MID, \
                                                                            request->info[0].data, \
                                                                            request->info[0].length, \
                                                                            &Current->Entry[0].Para.Input.Size);
                    Current->Entry[0].Object = CosemLoadAttribute(desc.descriptor.classid, desc.descriptor.index, false);
                    break;
                }
                case SET_FIRST_BLOCK:
                {
                    heap.set(&Current->Entry[0], 0, sizeof(Current->Entry[0]));
                    
                    desc.descriptor.classid = request->info[0].classid[0];
                    desc.descriptor.classid <<= 8;
                    desc.descriptor.classid += request->info[0].classid[1];
                    heap.copy(desc.descriptor.obis, request->info[0].obis, 6);
                    desc.descriptor.index = request->info[0].index[0];
                    desc.descriptor.selector = 0;
                    dlms_lex_parse(&desc, (union __dlms_right *)&Current->Entry[0].Right, \
                                   &Current->Entry[0].Para.Input.OID, \
                                   &Current->Entry[0].Para.Input.MID);
                    
                    if(!request->info->block)
                    {
                        return(APPL_BLOCK_MISS);
                    }
                    
                    Current->Block = request->info->block;//块计数
                    Current->Actived = 1;//一个请求条目
                    
                    Current->Entry[0].Para.Input.Buffer = request->info[0].data;//赋值数据
                    Current->Entry[0].Para.Input.Size = request->info[0].length;//赋值数据长度
                    if((request->info->end) && !(*(request->info->end)))
                    {
                        Current->Entry[0].Para.Iterator.Status = ITER_ONGOING;//赋值迭代标识
                    }
                    else
                    {
                        Current->Entry[0].Para.Iterator.Status = ITER_FINISHED;//赋值迭代标识
                    }
                    Current->Entry[0].Object = CosemLoadAttribute(desc.descriptor.classid, desc.descriptor.index, false);
                    break;
                }
                case SET_WITH_BLOCK:
                {
                    //不支持多实例
                    if((Current->Actived != 1) || (!Current->Entry[0].Object))
                    {
                        return(APPL_OBJ_OVERFLOW);
                    }
                    
                    //块计数验证
                    if((Current->Block + 1) != request->info->block)
                    {
                        return(APPL_BLOCK_MISS);
                    }
                    
                    Current->Block = request->info->block;//更新块计数
                    Current->Entry[0].Para.Input.Buffer = request->info[0].data;//赋值数据
                    Current->Entry[0].Para.Input.Size = request->info[0].length;//赋值数据长度
                    
                    if(!(request->info->end) || *(request->info->end))
                    {
                        Current->Entry[0].Para.Iterator.Status = ITER_FINISHED;//赋值迭代标识
                    }
                    break;
                }
                case SET_WITH_LIST:
                case SET_WITH_LIST_AND_FIRST_BLOCK:
                default:
                {
                    return(APPL_UNSUPPORT);
                }
            }
            break;
        }
        case ACTION_REQUEST:
        case GLO_ACTION_REQUEST:
        case DED_ACTION_REQUEST:
        {
            switch(request->type)
            {
                case ACTION_NORMAL:
                {
                    heap.set(&Current->Entry[0], 0, sizeof(Current->Entry[0]));
                    
                    desc.descriptor.classid = request->info[0].classid[0];
                    desc.descriptor.classid <<= 8;
                    desc.descriptor.classid += request->info[0].classid[1];
                    heap.copy(desc.descriptor.obis, request->info[0].obis, 6);
                    desc.descriptor.index = request->info[0].index[0];
                    desc.descriptor.selector = 0;
                    dlms_lex_parse(&desc, (union __dlms_right *)&Current->Entry[0].Right, \
                                   &Current->Entry[0].Para.Input.OID, \
                                   &Current->Entry[0].Para.Input.MID);
                    
                    Current->Block = 0;//块计数清零
                    Current->Actived = 1;//一个请求条目
                    
                    Current->Entry[0].Para.Input.Buffer = request->info[0].data;//赋值数据
                    Current->Entry[0].Para.Input.Size = request->info[0].length;//赋值数据长度
                    Current->Entry[0].Object = CosemLoadMethod(desc.descriptor.classid, desc.descriptor.index);
                    break;
                }
                case ACTION_NEXT_BLOCK:
                {
                    //不支持多实例
                    if((Current->Actived != 1) || (!Current->Entry[0].Object))
                    {
                        return(APPL_OBJ_OVERFLOW);
                    }
                    
                    //块计数验证
                    if((Current->Block + 1) != request->info->block)
                    {
                        return(APPL_BLOCK_MISS);
                    }
                    
                    Current->Block = request->info->block;//更新块计数
                    Current->Entry[0].Para.Input.Buffer = request->info[0].data;//赋值数据
                    Current->Entry[0].Para.Input.Size = request->info[0].length;//赋值数据长度
                    
                    if(!(request->info->end) || *(request->info->end))
                    {
                        Current->Entry[0].Para.Iterator.Status = ITER_FINISHED;//赋值迭代标识
                    }
                    break;
                }
                case ACTION_FIRST_BLOCK:
                {
                    heap.set(&Current->Entry[0], 0, sizeof(Current->Entry[0]));
                    
                    desc.descriptor.classid = request->info[0].classid[0];
                    desc.descriptor.classid <<= 8;
                    desc.descriptor.classid += request->info[0].classid[1];
                    heap.copy(desc.descriptor.obis, request->info[0].obis, 6);
                    desc.descriptor.index = request->info[0].index[0];
                    desc.descriptor.selector = 0;
                    dlms_lex_parse(&desc, (union __dlms_right *)&Current->Entry[0].Right, \
                                   &Current->Entry[0].Para.Input.OID, \
                                   &Current->Entry[0].Para.Input.MID);
                    
                    if(!request->info->block)
                    {
                        return(APPL_BLOCK_MISS);
                    }
                    
                    Current->Block = request->info->block;//块计数
                    Current->Actived = 1;//一个请求条目
                    
                    Current->Entry[0].Para.Input.Buffer = request->info[0].data;//赋值数据
                    Current->Entry[0].Para.Input.Size = request->info[0].length;//赋值数据长度
                    if((request->info->end) && !(*(request->info->end)))
                    {
                        Current->Entry[0].Para.Iterator.Status = ITER_ONGOING;//赋值迭代标识
                    }
                    else
                    {
                        Current->Entry[0].Para.Iterator.Status = ITER_FINISHED;//赋值迭代标识
                    }
                    Current->Entry[0].Object = CosemLoadMethod(desc.descriptor.classid, desc.descriptor.index);
                    break;
                }
                case ACTION_WITH_LIST:
                {
                    Current->Block = 0;//块计数清零
                    Current->Actived = 0;//一个请求条目
                    
                    for(cnt=0; cnt<DLMS_REQ_LIST_MAX; cnt++)
                    {
                        if(!request->info[cnt].active)
                        {
                            break;
                        }
                        
                        heap.set(&Current->Entry[cnt], 0, sizeof(Current->Entry[cnt]));
                        
                        desc.descriptor.classid = request->info[cnt].classid[0];
                        desc.descriptor.classid <<= 8;
                        desc.descriptor.classid += request->info[cnt].classid[1];
                        heap.copy(desc.descriptor.obis, request->info[cnt].obis, 6);
                        desc.descriptor.index = request->info[cnt].index[0];
                        desc.descriptor.selector = 0;
                        dlms_lex_parse(&desc, (union __dlms_right *)&Current->Entry[cnt].Right, \
                                       &Current->Entry[cnt].Para.Input.OID, \
                                       &Current->Entry[cnt].Para.Input.MID);
                        
                        Current->Actived += 1;//一个请求条目
                        
                        Current->Entry[cnt].Para.Input.Buffer = request->info[cnt].data;//赋值数据
                        Current->Entry[cnt].Para.Input.Size = request->info[cnt].length;//赋值数据长度
                        Current->Entry[cnt].Object = CosemLoadMethod(desc.descriptor.classid, desc.descriptor.index);
                    }
                    break;
                }
                case ACTION_WITH_LIST_AND_FIRST_BLOCK:
                case ACTION_WITH_BLOCK:
                default:
                {
                    return(APPL_UNSUPPORT);
                }
            }
            break;
        }
        default:
        {
            return(APPL_UNSUPPORT);
        }
    }
    
    return(APPL_SUCCESS);
}

/**
  * @brief 判断是否有访问权限
  * 
  */
static bool check_accessibility(struct __appl_request *request, struct __cosem_instance *instence)
{
    if(!instence)
    {
        return(false);
    }
    
    switch(request->service)
    {
        case GET_REQUEST:
        {
            if(instence->Right & 0xfc)
            {
                return(false);
            }
            
            if(!(instence->Right & ATTR_READ))
            {
                return(false);
            }
            
            break;
        }
        case SET_REQUEST:
        {
            if(instence->Right & 0xfc)
            {
                return(false);
            }
            
            if(!(instence->Right & ATTR_WRITE))
            {
                return(false);
            }
            
            break;
        }
        case GLO_GET_REQUEST:
        case DED_GET_REQUEST:
        case GLO_SET_REQUEST:
        case DED_SET_REQUEST:
        {
            if((instence->Right & ATTR_AUTHREQ) || (instence->Right & ATTR_AUTHRSP))
            {
                if(!(request->sc & 0x10))
                {
                    return(false);
                }
            }
            
            if((instence->Right & ATTR_ENCREQ) || (instence->Right & ATTR_ENCRSP))
            {
                if(!(request->sc & 0x20))
                {
                    return(false);
                }
            }
            
            if((instence->Right & ATTR_DIGITREQ) || (instence->Right & ATTR_DIGITRSP))
            {
                if((request->sc & 0x30) != 0x30)
                {
                    return(false);
                }
            }
            
            break;
        }
        case ACTION_REQUEST:
        {
            if(instence->Right & 0xfe)
            {
                return(false);
            }
            
            if(!(instence->Right & METHOD_ACCESS))
            {
                return(false);
            }
            
            break;
        }
        case GLO_ACTION_REQUEST:
        case DED_ACTION_REQUEST:
        {
            if((instence->Right & METHOD_AUTHREQ) || (instence->Right & METHOD_AUTHRSP))
            {
                if(!(request->sc & 0x10))
                {
                    return(false);
                }
            }
            
            if((instence->Right & METHOD_ENCREQ) || (instence->Right & METHOD_ENCRSP))
            {
                if(!(request->sc & 0x20))
                {
                    return(false);
                }
            }
            
            if((instence->Right & METHOD_DIGITREQ) || (instence->Right & METHOD_DIGITRSP))
            {
                if((request->sc & 0x30) != 0x30)
                {
                    return(false);
                }
            }
            
            break;
        }
    }
    
    return(true);
}

/**	
  * @brief 生成回复报文（随机数生成器）
  */
static int rng(void *p, unsigned char *buf, size_t blen)
{
	dlms_asso_random(blen, buf);
	
	return(0);
}

/**	
  * @brief 生成回复报文
  */
static enum __appl_result reply_normal(struct __appl_request *request, \
                                       uint8_t *buffer, \
                                       uint16_t buffer_length, \
                                       uint16_t *filled_length)
{
    uint8_t cnt = 0;
	uint8_t service;
    uint8_t *plain = (uint8_t *)0;
    uint16_t plain_length = 0;
    
    uint8_t akey[32] = {0};
    uint8_t akey_length = 0;
    uint8_t ekey[32] = {0};
    uint8_t ekey_length = 0;
    uint8_t iv[12] = {0};
    uint16_t cipher_length = 0;
    uint8_t tag[12] = {0};
    uint8_t *add = (uint8_t *)0;
    mbedtls_gcm_context ctx;
    int ret;
    
    //step 1
    //获取临时缓冲，用于组包未加密报文
    plain = heap.dalloc(dlms_asso_mtu() + 16);
    
    if(!plain)
    {
        return(APPL_NOMEM);
    }
    
    //step 2
    //组包返回的报文（明文）
	if((request->service == GNL_GLO_CIPHER_REQUEST) || \
		(request->service == GNL_DED_CIPHER_REQUEST) || \
		(request->service == GNL_SIGN_REQUEST))
	{
		service = request->plain[0];
	}
	else
	{
		service = request->service;
	}
	
    switch(service)
    {
        case GET_REQUEST:
        case GLO_GET_REQUEST:
        case DED_GET_REQUEST:
        {
            plain[0] = GET_RESPONSE;
            plain[2] = request->id;
            
            plain_length = 3;
            
            switch(request->type)
            {
                case GET_NORMAL:
                {
                    if(!Current->Entry[0].Object) //Get-Response-Normal (Error)
                    {
                        plain[1] = GET_RESPONSE_NORMAL;
                        plain[3] = 1;
                        plain[4] = (uint8_t)OBJECT_ERR_MEM;
                        plain_length = 5;
                    }
                    else if(Current->Entry[0].Para.Iterator.Status != ITER_NONE) //Get-Response-With-Datablock
                    {
                        if((Current->Entry[0].Para.Output.Filled > Current->Entry[0].Para.Output.Size) || \
                            (Current->Entry[0].Para.Output.Filled > (dlms_asso_mtu() - 20)))
                        {
                            plain[1] = GET_RESPONSE_NORMAL;
                            plain[3] = 1;
                            plain[4] = (uint8_t)OBJECT_ERR_MEM;
                            plain_length = 5;
                            
                            Current->Entry[0].Para.Iterator.Status = ITER_NONE;
                        }
                        else
                        {
                            if((Current->Entry[0].Para.Iterator.Status == ITER_FINISHED) || \
                                (Current->Entry[0].Para.Iterator.From == Current->Entry[0].Para.Iterator.To))
                            {
                                plain[1] = GET_RESPONSE_NORMAL;
                                plain[3] = 0;
                                plain_length = 4;
                                plain_length += response_formatter(Current->Entry[0].Para.Input.MID, \
                                                                   Current->Entry[0].Para.Output.Buffer, \
                                                                   Current->Entry[0].Para.Output.Filled, \
                                                                   &plain[4]);
                            }
                            else
                            {
                                plain[1] = GET_RESPONSE_WITH_BLOCK;
                                plain[3] = (uint8_t)NOT_LAST_BLOCK;
                                
                                plain[4] = (uint8_t)(Current->Block >> 24);
                                plain[5] = (uint8_t)(Current->Block >> 16);
                                plain[6] = (uint8_t)(Current->Block >> 8);
                                plain[7] = (uint8_t)(Current->Block >> 0);
                                
                                plain[8] = 0;
                                
                                plain_length = 9;
                                
                                plain_length += axdr.length.encode(Current->Entry[0].Para.Output.Filled, &plain[9]);
                                plain_length += heap.copy(&plain[plain_length], \
                                                          Current->Entry[0].Para.Output.Buffer, \
                                                          Current->Entry[0].Para.Output.Filled);
                            }
                        }
                    }
                    else //Get-Response-Normal
                    {
                        plain[1] = GET_RESPONSE_NORMAL;
                        if(Current->Entry[0].Errs == OBJECT_NOERR)
                        {
                            if((Current->Entry[0].Para.Output.Filled > Current->Entry[0].Para.Output.Size) || \
                                (Current->Entry[0].Para.Output.Filled > (dlms_asso_mtu() - 20)))
                            {
                                plain[3] = 1;
                                plain[4] = (uint8_t)OBJECT_ERR_MEM;
                                plain_length = 5;
                            }
                            else
                            {
                                plain[3] = 0;
                                plain_length = 4;
                                plain_length += response_formatter(Current->Entry[0].Para.Input.MID, \
                                                                   Current->Entry[0].Para.Output.Buffer, \
                                                                   Current->Entry[0].Para.Output.Filled, \
                                                                   &plain[4]);
                            }
                        }
                        else
                        {
                            plain[3] = 1;
                            plain[4] = (uint8_t)Current->Entry[0].Errs;
                            plain_length = 5;
                        }
                    }
                    
                    break;
                }
                case GET_NEXT:
                {
                    plain[1] = GET_RESPONSE_WITH_BLOCK;
                    
                    if(!Current->Entry[0].Object)
                    {
                        plain[3] = 1;
                        plain[4] = (uint8_t)OBJECT_ERR_MEM;
                        plain_length = 5;
                    }
                    else if(Current->Entry[0].Para.Iterator.Status == ITER_NONE)
                    {
                        plain[3] = 1;
                        plain[4] = (uint8_t)OBJECT_ERR_NODEF;
                        plain_length = 5;
                    }
                    else
                    {
                        if((Current->Entry[0].Para.Output.Filled > Current->Entry[0].Para.Output.Size) || \
                            (Current->Entry[0].Para.Output.Filled > (dlms_asso_mtu() - 20)))
                        {
                            plain[3] = 1;
                            plain[4] = (uint8_t)OBJECT_ERR_MEM;
                            plain_length = 5;
                            
                            Current->Entry[0].Para.Iterator.Status = ITER_NONE;
                        }
                        else
                        {
                            if((Current->Entry[0].Para.Iterator.Status == ITER_FINISHED) || \
                                (Current->Entry[0].Para.Iterator.From == Current->Entry[0].Para.Iterator.To))
                            {
                                plain[3] = (uint8_t)IS_LAST_BLOCK;
                            }
                            else
                            {
                                plain[3] = (uint8_t)NOT_LAST_BLOCK;
                            }
                            
                            plain[4] = (uint8_t)(Current->Block >> 24);
                            plain[5] = (uint8_t)(Current->Block >> 16);
                            plain[6] = (uint8_t)(Current->Block >> 8);
                            plain[7] = (uint8_t)(Current->Block >> 0);
                            
                            plain[8] = 0;
                            
                            plain_length = 9;
                            
                            plain_length += axdr.length.encode(Current->Entry[0].Para.Output.Filled, &plain[9]);
                            plain_length += heap.copy(&plain[plain_length], \
                                                      Current->Entry[0].Para.Output.Buffer, \
                                                      Current->Entry[0].Para.Output.Filled);
                        }
                    }
                    break;
                }
                case GET_WITH_LIST:
                {
                    plain[1] = GET_RESPONSE_WITH_LIST;
                    
                    for(cnt=0; cnt<DLMS_REQ_LIST_MAX; cnt++)
                    {
                        if(!Current->Entry[cnt].Object)
                        {
                            continue;
                        }
                        
                        //Get-Response-With-List 不允许 Get-Response-With-Datablock
                        if((Current->Entry[cnt].Para.Iterator.Status != ITER_NONE) || \
                            (Current->Entry[cnt].Para.Iterator.From != Current->Entry[cnt].Para.Iterator.To))
                        {
                            Current->Entry[cnt].Para.Iterator.Status = ITER_NONE;
                            Current->Entry[cnt].Errs = OBJECT_ERR_MEM;
                        }
                        
                        if(Current->Entry[cnt].Errs == OBJECT_NOERR)
                        {
                            if((Current->Entry[cnt].Para.Output.Filled > Current->Entry[cnt].Para.Output.Size) || \
                                ((Current->Entry[cnt].Para.Output.Filled + plain_length) > (dlms_asso_mtu() - 20)))
                            {
                                plain[3] = 1;
                                plain[4] = (uint8_t)OBJECT_ERR_MEM;
                                plain_length = 5;
                                break;
                            }
                            else
                            {
                                plain[plain_length + 0] = 0;
                                plain_length += 1;
                                
                                plain_length += response_formatter(Current->Entry[cnt].Para.Input.MID, \
                                                                   Current->Entry[cnt].Para.Output.Buffer, \
                                                                   Current->Entry[cnt].Para.Output.Filled, \
                                                                   &plain[plain_length]);
                            }
                        }
                        else
                        {
                            plain[plain_length + 0] = 1;
                            plain[plain_length + 1] = (uint8_t)Current->Entry[cnt].Errs;
                            plain_length += 2;
                        }
                    }
                    
                    if(plain_length == 3)
                    {
                        plain[3] = 1;
                        plain[4] = (uint8_t)OBJECT_ERR_MEM;
                        plain_length = 5;
                    }
                    break;
                }
                default:
                {
                    heap.free(plain);
                    return(APPL_UNSUPPORT);
                }
            }
            break;
        }
        case SET_REQUEST:
        case GLO_SET_REQUEST:
        case DED_SET_REQUEST:
        {
            plain[0] = SET_RESPONSE;
            plain[2] = request->id;
            
            switch(request->type)
            {
                case SET_NORMAL:
                {
                    plain[1] = SET_RESPONSE_NORMAL;
                    
                    if(!Current->Entry[0].Object || Current->Actived != 1)
                    {
                        plain[3] = (uint8_t)OBJECT_ERR_MEM;
                        plain_length = 4;
                    }
                    else
                    {
                        if((Current->Entry[0].Para.Iterator.Status != ITER_NONE) || \
                            (Current->Entry[0].Para.Iterator.From != Current->Entry[0].Para.Iterator.To))
                        {
                            Current->Entry[0].Para.Iterator.Status = ITER_NONE;
                            Current->Entry[0].Errs = OBJECT_ERR_MEM;
                        }
                        
                        if(Current->Entry[cnt].Errs == OBJECT_NOERR)
                        {
                            plain[3] = 0;
                            plain_length = 4;
                        }
                        else
                        {
                            plain[3] = (uint8_t)Current->Entry[cnt].Errs;
                            plain_length = 4;
                        }
                    }
                    break;
                }
                case SET_FIRST_BLOCK:
                case SET_WITH_BLOCK:
                {
                    if(!Current->Entry[0].Object || Current->Actived != 1)
                    {
                        plain[1] = SET_RESPONSE_NORMAL;
                        plain[3] = (uint8_t)OBJECT_ERR_MEM;
                        plain_length = 4;
                    }
                    else
                    {
                        if((Current->Entry[0].Para.Iterator.Status != ITER_ONGOING) || \
                            (Current->Entry[0].Errs != OBJECT_NOERR))
                        {
                            plain[1] = SET_RESPONSE_LAST_DATABLOCK;
                            plain[3] = (uint8_t)Current->Entry[0].Errs;
                            plain[4] = Current->Block >> 24;
                            plain[5] = Current->Block >> 16;
                            plain[6] = Current->Block >> 8;
                            plain[7] = Current->Block >> 0;
                            
                            plain_length = 8;
                        }
                        else
                        {
                            plain[1] = SET_RESPONSE_DATABLOCK;
                            plain[3] = Current->Block >> 24;
                            plain[4] = Current->Block >> 16;
                            plain[5] = Current->Block >> 8;
                            plain[6] = Current->Block >> 0;
                            
                            plain_length = 7;
                        }
                    }
                    break;
                }
                case SET_WITH_LIST:
                case SET_WITH_LIST_AND_FIRST_BLOCK:
                default:
                {
                    heap.free(plain);
                    return(APPL_UNSUPPORT);
                }
            }
            
            break;
        }
        case ACTION_REQUEST:
        case GLO_ACTION_REQUEST:
        case DED_ACTION_REQUEST:
        {
            plain[0] = ACTION_RESPONSE;
            plain[2] = request->id;
            
            switch(request->type)
            {
                case ACTION_NORMAL:
                {
                    plain[1] = ACTION_RESPONSE_NORMAL;
                    
                    if(!Current->Entry[0].Object || Current->Actived != 1)
                    {
                        plain[3] = (uint8_t)OBJECT_ERR_MEM;
                        plain[4] = 0;
                        plain_length = 5;
                    }
                    else
                    {
                        if((Current->Entry[0].Para.Iterator.Status != ITER_NONE) || \
                            (Current->Entry[0].Para.Iterator.From != Current->Entry[0].Para.Iterator.To))
                        {
                            Current->Entry[0].Para.Iterator.Status = ITER_NONE;
                            Current->Entry[0].Errs = OBJECT_ERR_MEM;
                        }
                        
                        if(Current->Entry[0].Errs == OBJECT_NOERR)
                        {
                            if((Current->Entry[0].Para.Output.Filled > Current->Entry[0].Para.Output.Size) || \
                                ((Current->Entry[0].Para.Output.Filled + plain_length) > (dlms_asso_mtu() - 20)))
                            {
                                plain[3] = (uint8_t)OBJECT_ERR_MEM;
                                plain[4] = 0;
                                plain_length = 5;
                            }
                            else
                            {
                                plain[3] = 0;
                                
                                if(Current->Entry[0].Para.Output.Filled)
                                {
                                    plain[4] = 1;
                                    plain[5] = 0;
                                    plain_length = 6;
                                    
                                    plain_length += heap.copy(&plain[6], \
                                                              Current->Entry[0].Para.Output.Buffer, \
                                                              Current->Entry[0].Para.Output.Filled);
                                }
                                else
                                {
                                    plain[4] = 0;
                                    plain_length = 5;
                                }
                            }
                        }
                        else
                        {
                            plain[3] = (uint8_t)OBJECT_ERR_MEM;
                            plain[4] = 0;
                            plain_length = 5;
                        }
                    }
                    break;
                }
                case ACTION_NEXT_BLOCK:
                case ACTION_FIRST_BLOCK:
                case ACTION_WITH_LIST:
                case ACTION_WITH_LIST_AND_FIRST_BLOCK:
                case ACTION_WITH_BLOCK:
                default:
                {
                    heap.free(plain);
                    return(APPL_UNSUPPORT);
                }
            }
            
            break;
        }
		default:
        {
            heap.free(plain);
            return(APPL_UNSUPPORT);
        }
    }
    
    //step 3
    //判断是否需要加密报文
    switch(request->service)
    {
        case GLO_GET_REQUEST:
        case GLO_SET_REQUEST:
        case GLO_ACTION_REQUEST:
		case GNL_GLO_CIPHER_REQUEST:
        {
            ekey_length = dlms_asso_ekey(ekey);
            akey_length = dlms_asso_akey(akey);
            dlms_asso_localtitle(iv);
            dlms_asso_fc(&iv[8]);
            break;
        }
        case DED_GET_REQUEST:
        case DED_SET_REQUEST:
        case DED_ACTION_REQUEST:
		case GNL_DED_CIPHER_REQUEST:
        {
            ekey_length = dlms_asso_dedkey(ekey);
            akey_length = dlms_asso_akey(akey);
            dlms_asso_localtitle(iv);
            dlms_asso_fc(&iv[8]);
            break;
        }
		case GNL_SIGN_REQUEST:
		{
            if((request->general.sign.content[0] == GLO_GET_REQUEST) || \
					(request->general.sign.content[0] == GLO_SET_REQUEST) || \
					(request->general.sign.content[0] == GLO_ACTION_REQUEST))
			{
				ekey_length = dlms_asso_ekey(ekey);
				akey_length = dlms_asso_akey(akey);
				dlms_asso_localtitle(iv);
				dlms_asso_fc(&iv[8]);
			}
			else if((request->general.sign.content[0] == DED_GET_REQUEST) || \
				(request->general.sign.content[0] == DED_SET_REQUEST) || \
				(request->general.sign.content[0] == DED_ACTION_REQUEST))
            {
				ekey_length = dlms_asso_dedkey(ekey);
				akey_length = dlms_asso_akey(akey);
				dlms_asso_localtitle(iv);
				dlms_asso_fc(&iv[8]);
            }
			else if((request->general.sign.content[0] == GET_REQUEST) || \
					(request->general.sign.content[0] == SET_REQUEST) || \
					(request->general.sign.content[0] == ACTION_REQUEST))
            {
				break;
            }
			else
			{
				heap.free(plain);
				return(APPL_ENC_FAILD);
			}
			break;
		}
        default:
        {
            //不需要加密，明文返回
            if(plain_length > buffer_length)
            {
                plain_length = buffer_length;
            }
            
            *filled_length = heap.copy(buffer, plain, plain_length);
            
            heap.free(plain);
            return(APPL_SUCCESS);
        }
    }
    
    //step 4
    //加密
    switch(request->service)
    {
        case GLO_GET_REQUEST:
        	buffer[0] = GLO_GET_RESPONSE;
        	break;
        case GLO_SET_REQUEST:
        	buffer[0] = GLO_SET_RESPONSE;
        	break;
        case GLO_ACTION_REQUEST:
        	buffer[0] = GLO_ACTION_RESPONSE;
        	break;
        case DED_GET_REQUEST:
        	buffer[0] = DED_GET_RESPONSE;
        	break;
        case DED_SET_REQUEST:
        	buffer[0] = DED_SET_RESPONSE;
        	break;
        case DED_ACTION_REQUEST:
        	buffer[0] = DED_ACTION_RESPONSE;
        	break;
        case GNL_GLO_CIPHER_REQUEST:
        	buffer[0] = GNL_GLO_CIPHER_RESPONSE;
        	break;
        case GNL_DED_CIPHER_REQUEST:
        	buffer[0] = GNL_DED_CIPHER_RESPONSE;
        	break;
        case GNL_SIGN_REQUEST:
        	buffer[0] = GNL_SIGN_RESPONSE;
        	break;
    }
    
    cipher_length = 1;
	
	if((request->service == GNL_GLO_CIPHER_REQUEST) || (request->service == GNL_DED_CIPHER_REQUEST))
	{
		buffer[cipher_length] = 8;
		cipher_length += 1;
		dlms_asso_localtitle(&buffer[cipher_length]);
		cipher_length += 8;
	}
	else if(request->service == GNL_SIGN_REQUEST)
	{
		//留空，存放长度信息，最长3字节
		cipher_length += 3;
		
		//Transaction Id
		memcpy(&buffer[cipher_length], request->general.sign.transaction, 9);
		cipher_length += 9;
		//Originator System Title
		memcpy(&buffer[cipher_length], request->general.sign.recipient, 9);
		cipher_length += 9;
		//Recipient System Title
		memcpy(&buffer[cipher_length], request->general.sign.originator, 9);
		cipher_length += 9;
		//Date Time
		if(request->general.sign.date)
		{
			//...签名时间暂时使用接收的时间
			memcpy(&buffer[cipher_length], request->general.sign.date, 13);
			cipher_length += 13;
		}
		else
		{
			buffer[cipher_length] = 0;
			cipher_length += 1;
		}
		
		//Other_Information
		buffer[cipher_length] = 0;
		cipher_length += 1;
		
		switch(request->general.sign.content[0])
		{
			case GET_REQUEST:
			case SET_REQUEST:
			case ACTION_REQUEST:
			{
				heap.copy(&buffer[cipher_length], plain, plain_length);
				cipher_length += plain_length;
				break;
			}
			default:
			{
				buffer[cipher_length] = request->general.sign.content[0];
				cipher_length += 1;
				break;
			}
		}
	}
	
	if((request->sc & 0xf0) == 0x10)
	{
		cipher_length += axdr.length.encode((5+plain_length+12), &buffer[cipher_length]);
		
		if(buffer_length < (cipher_length+5+plain_length+12))
		{
			goto enc_faild;
		}
		
		buffer[cipher_length] = request->sc;
		cipher_length += 1;
		cipher_length += heap.copy(&buffer[cipher_length], &iv[8], 4);
		
		mbedtls_gcm_init(&ctx);
		
		ret = mbedtls_gcm_setkey(&ctx,
								 MBEDTLS_CIPHER_ID_AES,
								 ekey,
								 ekey_length*8);
		
		if(ret != 0)
		{
			goto enc_faild;
		}
		
		add = heap.dalloc(1 + akey_length + plain_length);
		if(!add)
		{
			goto enc_faild;
		}
		
		add[0] = request->sc;
		heap.copy(&add[1], akey, akey_length);
		heap.copy(&add[1+akey_length], plain, plain_length);
		
		ret = mbedtls_gcm_crypt_and_tag(&ctx,
										MBEDTLS_GCM_ENCRYPT,
										0,
										iv,
										sizeof(iv),
										add,
										(1 + akey_length + plain_length),
										(void *)0,
										(void *)0,
										sizeof(tag),
										tag);
		heap.free(add);
		
		if(ret != 0)
		{
			goto enc_faild;
		}
		
		mbedtls_gcm_free(&ctx);
		
		cipher_length += heap.copy(&buffer[cipher_length], plain, plain_length);
		cipher_length += heap.copy(&buffer[cipher_length], tag, sizeof(tag));
		*filled_length = cipher_length;
	}
	else if((request->sc & 0xf0) == 0x20)
	{
		cipher_length += axdr.length.encode((5+plain_length), &buffer[cipher_length]);
		
		if(buffer_length < (cipher_length+5+plain_length))
		{
			goto enc_faild;
		}
		
		buffer[cipher_length] = request->sc;
		cipher_length += 1;
		cipher_length += heap.copy(&buffer[cipher_length], &iv[8], 4);
		
		mbedtls_gcm_init(&ctx);
		
		ret = mbedtls_gcm_setkey(&ctx,
								 MBEDTLS_CIPHER_ID_AES,
								 ekey,
								 ekey_length*8);
		
		if(ret != 0)
		{
			goto enc_faild;
		}
		
		ret = mbedtls_gcm_crypt_and_tag(&ctx,
										MBEDTLS_GCM_ENCRYPT,
										plain_length,
										iv,
										sizeof(iv),
										(void *)0,
										0,
										plain,
										(buffer + cipher_length),
										0,
										(void *)0);
		if(ret != 0)
		{
			goto enc_faild;
		}
		
		mbedtls_gcm_free(&ctx);
		
		*filled_length = cipher_length + plain_length;
	}
	else if((request->sc & 0xf0) == 0x30)
	{
		cipher_length += axdr.length.encode((5+plain_length+12), &buffer[cipher_length]);
		
		if(buffer_length < (cipher_length+5+plain_length+12))
		{
			goto enc_faild;
		}
		
		buffer[cipher_length] = request->sc;
		cipher_length += 1;
		cipher_length += heap.copy(&buffer[cipher_length], &iv[8], 4);
		
		mbedtls_gcm_init(&ctx);
		
		ret = mbedtls_gcm_setkey(&ctx,
								 MBEDTLS_CIPHER_ID_AES,
								 ekey,
								 ekey_length*8);
		
		if(ret != 0)
		{
			goto enc_faild;
		}
		
		add = heap.dalloc(1 + akey_length);
		if(!add)
		{
			goto enc_faild;
		}
		
		add[0] = request->sc;
		heap.copy(&add[1], akey, akey_length);
		
		ret = mbedtls_gcm_crypt_and_tag(&ctx,
										MBEDTLS_GCM_ENCRYPT,
										plain_length,
										iv,
										sizeof(iv),
										add,
										(1 + akey_length),
										plain,
										(buffer + cipher_length),
										sizeof(tag),
										tag);
		heap.free(add);
		
		if(ret != 0)
		{
			goto enc_faild;
		}
		
		mbedtls_gcm_free(&ctx);
		
		cipher_length += plain_length;
		cipher_length += heap.copy((buffer + cipher_length), tag, sizeof(tag));
		
		*filled_length = cipher_length;
	}
	else
	{
		if(request->service != GNL_SIGN_REQUEST)
		{
			goto enc_faild;
		}
	}
    
    heap.free(plain);
	if(request->service == GNL_SIGN_REQUEST)
	{
		goto add_signature;
	}
	else
	{
    	return(APPL_SUCCESS);
	}

enc_faild:
    heap.free(plain);
    return(APPL_ENC_FAILD);

add_signature:
	heap.free(plain);
	return(APPL_SUCCESS);
}

/**	
  * @brief 生成回复报文（产生错误）
  */
static void reply_exception(enum __appl_result result, \
                            uint8_t *buffer, \
                            uint16_t buffer_length, \
                            uint16_t *filled_length)
{
    buffer[0] = EXCEPTION_RESPONSE;
    
    switch(result)
    {
        case APPL_DENIED:
        case APPL_BLOCK_MISS:
        case APPL_OBJ_NODEF:
        {
            buffer[1] = 1;
            buffer[2] = 1;
            break;
        }
        case APPL_NOMEM:
        case APPL_OBJ_MISS:
        case APPL_OBJ_OVERFLOW:
        case APPL_ENC_FAILD:
        {
            buffer[1] = 1;
            buffer[2] = 3;
            break;
        }
        case APPL_UNSUPPORT:
        {
            buffer[1] = 2;
            buffer[2] = 2;
            break;
        }
        case APPL_OTHERS:
        default:
        {
            buffer[1] = 2;
            buffer[2] = 3;
            break;
        }
    }
    
    *filled_length = 3;
}


/**	
  * @brief dlms规约入口
  */
void dlms_appl_entrance(const uint8_t *info,
                        uint16_t length,
                        uint8_t *buffer,
                        uint16_t buffer_length,
                        uint16_t *filled_length)
{
    uint8_t cnt = 0;
    uint8_t alive = 0;
    uint8_t *cosem_data = (uint8_t *)0;
    enum __appl_result result;
    struct __appl_request request;
    
    Current = (struct __cosem_request *)0;
    
    heap.set(&request, 0, sizeof(request));
    
    //step 1
    //解析报文
    //去除加密，并且索引报文中有效字段
    result = parse_dlms_frame(info, length, &request, true);
    
    //解析报文失败
    if(result != APPL_SUCCESS)
    {
        reply_exception(result, buffer, buffer_length, filled_length);
        return;
    }
    
    //step 2
    //生成访问对象
    result = make_cosem_instance(&request);
    
    //生成访问对象失败
    if((result != APPL_SUCCESS) || (!Current))
    {
        //清零访问对象
        if(Current)
        {
            heap.set(Current, 0, sizeof(struct __cosem_request));
            Current = (struct __cosem_request *)0;
        }
        
        reply_exception(result, buffer, buffer_length, filled_length);
        
        return;
    }
    
    //检查生成的访问对象是否合法
    if((Current->Actived == 0) || (Current->Actived > DLMS_REQ_LIST_MAX))
    {
        reply_exception(APPL_OBJ_OVERFLOW, buffer, buffer_length, filled_length);
        Current = (struct __cosem_request *)0;
        return;
    }
    else
    {
        for(cnt=0; cnt<DLMS_REQ_LIST_MAX; cnt++)
        {
            if(Current->Entry[cnt].Object)
            {
                alive += 1;
            }
        }
        
        if(Current->Actived != alive)
        {
            reply_exception(APPL_OBJ_OVERFLOW, buffer, buffer_length, filled_length);
            Current = (struct __cosem_request *)0;
            return;
        }
    }
    
    //step 3
    //获取访问对象对应的输出缓冲
    cosem_data = heap.dalloc((dlms_asso_mtu() + Current->Actived * 16));
    if(!cosem_data)
    {
        //清零链接内存
        heap.set(Current, 0, sizeof(struct __cosem_request));
        reply_exception(APPL_NOMEM, buffer, buffer_length, filled_length);
        Current = (struct __cosem_request *)0;
        return;
    }
    
    //均分内存给访问对象内的有效条目
    alive = 0;
    for(cnt=0; cnt<DLMS_REQ_LIST_MAX; cnt++)
    {
        if(Current->Entry[cnt].Object)
        {
            Current->Entry[cnt].Para.Output.Buffer = cosem_data + (dlms_asso_mtu() / Current->Actived + 16) * alive;
            Current->Entry[cnt].Para.Output.Size = (dlms_asso_mtu() - 20) / Current->Actived;
            alive += 1;
        }
    }
    
    //step 4
    //遍历访问每个条目
    for(cnt=0; cnt<DLMS_REQ_LIST_MAX; cnt++)
    {
        if(Current->Entry[cnt].Object)
        {
            if(request.info[cnt].active)
            {
                instance_name = request.info[cnt].obis;
            }
            
            //判断是否有访问权限
            if(check_accessibility(&request, &Current->Entry[cnt]))
            {
                Current->Entry[cnt].Errs = Current->Entry[cnt].Object(&Current->Entry[cnt].Para);
            }
            else
            {
				if((request.service == ACTION_REQUEST) || \
				(request.service == GLO_ACTION_REQUEST) || \
				(request.service == DED_ACTION_REQUEST))
				{
					Current->Entry[cnt].Errs = (ObjectErrs)ACTION_REAS_WRITE_DENIED;
				}
				else
				{
					Current->Entry[cnt].Errs = (ObjectErrs)DATA_REAS_WRITE_DENIED;
				}
            }
            
            instance_name = (uint8_t *)0;
        }
    }
    
    //step 5
    //组包返回数据
    result = reply_normal(&request, buffer, buffer_length, filled_length);
    
    if(result != APPL_SUCCESS)
    {
        reply_exception(result, buffer, buffer_length, filled_length);
    }
    
    //step 6
    //判断条目生命周期是否结束
    for(cnt=0; cnt<DLMS_REQ_LIST_MAX; cnt++)
    {
        if(Current->Entry[cnt].Object)
        {
            //迭代器状态不在运行中，或者迭代器起始等于终止，或者调用结果为异常，生命周期结束
            if((Current->Entry[cnt].Para.Iterator.Status != ITER_ONGOING) || \
                (Current->Entry[cnt].Para.Iterator.From == Current->Entry[cnt].Para.Iterator.To) || \
                Current->Entry[cnt].Errs != OBJECT_NOERR)
            {
                //清零条目
                heap.set((void *)&Current->Entry[cnt], 0, sizeof(Current->Entry[cnt]));
            }
        }
    }
    
    //重新计算激活的条目数
    Current->Actived = 0;
    for(cnt=0; cnt<DLMS_REQ_LIST_MAX; cnt++)
    {
        if(Current->Entry[cnt].Object)
        {
            Current->Actived += 1;
        }
    }
    
    //释放内存
    heap.free(cosem_data);
    
    Current = (struct __cosem_request *)0;
}

/**	
  * @brief dlms获取当前正在访问的数据项的实例名
  */
uint8_t dlms_appl_instance(uint8_t *name)
{
    if(name && instance_name)
    {
        heap.copy(name, instance_name, 6);
        
        return(6);
    }
    
    return(0);
}
