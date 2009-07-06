#include <sys/types.h>
#include <stdlib.h>
#if defined (__sun) || defined(_AIX)
#  include <alloca.h>
#endif
#ifdef _WIN32
#  include <malloc.h>
#endif
#include <errno.h>
#include <ffi.h>
#include <jni.h>
#include "jffi.h"
#include "Exception.h"
#include "Function.h"
#include "Array.h"
#include "LastError.h"
#include "com_kenai_jffi_Foreign.h"

#define PARAM_SIZE (8)
#define MAX_STACK_ARGS (8)

#ifdef _WIN32
typedef char* caddr_t;
#endif

/*
 * Always align memory on a 8 byte boundary
 */
#define MIN_ALIGN (8)
#define alloca_aligned(size, align) \
    ((void *) ((((uintptr_t) alloca(size + align - 1)) & ~(align - 1)) + align))

#ifdef USE_RAW
static void
invokeArray(JNIEnv* env, jlong ctxAddress, jbyteArray paramBuffer, void* returnBuffer)
{
    Function* ctx = (Function *) j2p(ctxAddress);
    union { double d; long long ll; jbyte tmp[PARAM_SIZE]; } tmpStackBuffer[MAX_STACK_ARGS];
    jbyte *tmpBuffer = (jbyte *) &tmpStackBuffer[0];
    
    if (likely(ctx->cif.nargs > 0)) {
        if (unlikely(ctx->cif.bytes > (int) sizeof(tmpStackBuffer))) {
            tmpBuffer = alloca_aligned(ctx->cif.bytes, MIN_ALIGN);
        }

        (*env)->GetByteArrayRegion(env, paramBuffer, 0, ctx->rawParameterSize, tmpBuffer);
    }

    ffi_raw_call(&ctx->cif, FFI_FN(ctx->function), returnBuffer, (ffi_raw *) tmpBuffer);
    SAVE_ERRNO(ctx);
}

#else
static void
invokeArray(JNIEnv* env, jlong ctxAddress, jbyteArray paramBuffer, void* returnBuffer)
{
    Function* ctx = (Function *) j2p(ctxAddress);
    union { double d; long long ll; jbyte tmp[PARAM_SIZE]; } tmpStackBuffer[MAX_STACK_ARGS];
    jbyte *tmpBuffer = (jbyte *) &tmpStackBuffer[0];
    void* ffiStackArgs[MAX_STACK_ARGS];
    void** ffiArgs = ffiStackArgs;
    
    if (ctx->cif.nargs > 0) {
        unsigned int i;
        if (ctx->cif.nargs > MAX_STACK_ARGS) {
            tmpBuffer = alloca_aligned(ctx->cif.nargs * PARAM_SIZE, MIN_ALIGN);
            ffiArgs = alloca_aligned(ctx->cif.nargs * sizeof(void *), MIN_ALIGN);
        }
        
        (*env)->GetByteArrayRegion(env, paramBuffer, 0, ctx->cif.nargs * PARAM_SIZE, tmpBuffer);

        for (i = 0; i < ctx->cif.nargs; ++i) {
            if (ctx->cif.arg_types[i]->type == FFI_TYPE_STRUCT) {
                ffiArgs[i] = *(void **) &tmpBuffer[i * PARAM_SIZE];
            } else {
                ffiArgs[i] = &tmpBuffer[i * PARAM_SIZE];
            }
        }
    }

    ffi_call(&ctx->cif, FFI_FN(ctx->function), returnBuffer, ffiArgs);
    SAVE_ERRNO(ctx);
}
#endif
/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    isRawParameterPackingEnabled
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL
Java_com_kenai_jffi_Foreign_isRawParameterPackingEnabled(JNIEnv* env, jobject self)
{
#ifdef USE_RAW
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}
/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayInt32
 * Signature: (J[B)I
 */
JNIEXPORT jint JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayReturnInt(JNIEnv* env, jclass self, jlong ctxAddress,
        jbyteArray paramBuffer)
{
    FFIValue retval;
    invokeArray(env, ctxAddress, paramBuffer, &retval);
    return_int(retval);
}

/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayInt64
 * Signature: (J[B)J
 */
JNIEXPORT jlong JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayReturnLong(JNIEnv* env, jclass self, jlong ctxAddress,
        jbyteArray paramBuffer)
{
    FFIValue retval;
    invokeArray(env, ctxAddress, paramBuffer, &retval);
    return retval.s64;
}

/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayFloat
 * Signature: (J[B)F
 */
JNIEXPORT jfloat JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayReturnFloat(JNIEnv* env, jclass self, jlong ctxAddress,
        jbyteArray paramBuffer)
{
    FFIValue retval;
    invokeArray(env, ctxAddress, paramBuffer, &retval);
    return retval.f;
}
/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayDouble
 * Signature: (J[B)D
 */
JNIEXPORT jdouble JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayReturnDouble(JNIEnv* env, jclass self, jlong ctxAddress,
        jbyteArray paramBuffer)
{
    FFIValue retval;
    invokeArray(env, ctxAddress, paramBuffer, &retval);
    return retval.d;
}

/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayReturnStruct
 * Signature: (J[B[B)V
 */
JNIEXPORT void JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayReturnStruct(JNIEnv* env, jclass self, jlong ctxAddress,
        jbyteArray paramBuffer, jbyteArray returnBuffer, jint offset)
{
    Function* ctx = (Function *) j2p(ctxAddress);
    jbyte* retval = alloca_aligned(ctx->cif.rtype->size, MIN_ALIGN);
    jbyte* tmpBuffer;
    void** ffiArgs;
    int i;

    //
    // Due to the undocumented and somewhat strange struct-return handling when
    // using ffi_raw_call(), we convert from raw to ptr array, then call via normal
    // ffi_call
    //

    ffiArgs = alloca(ctx->cif.nargs * sizeof(void *));

#ifdef USE_RAW
    tmpBuffer = alloca(ctx->rawParameterSize);
    (*env)->GetByteArrayRegion(env, paramBuffer, 0, ctx->rawParameterSize, tmpBuffer);
    ffi_raw_to_ptrarray(&ctx->cif, (ffi_raw *) tmpBuffer, ffiArgs);
#else
    tmpBuffer = alloca_aligned(ctx->cif.nargs * PARAM_SIZE, MIN_ALIGN);
    (*env)->GetByteArrayRegion(env, paramBuffer, 0, ctx->cif.nargs * PARAM_SIZE, tmpBuffer);
    for (i = 0; i < ctx->cif.nargs; ++i) {
        ffiArgs[i] = &tmpBuffer[i * PARAM_SIZE];
    }
#endif
    ffi_call(&ctx->cif, FFI_FN(ctx->function), retval, ffiArgs);
    SAVE_ERRNO(ctx);
    (*env)->SetByteArrayRegion(env, returnBuffer, offset, ctx->cif.rtype->size, retval);
}

#define MAX_STACK_OBJECTS (4)

static void
invokeArrayWithObjects_(JNIEnv* env, jlong ctxAddress, jbyteArray paramBuffer,
        jint objectCount, jint* infoBuffer, jobject* objectBuffer, void* retval)
{
    Function* ctx = (Function *) j2p(ctxAddress);
    union { double d; long long ll; jbyte tmp[PARAM_SIZE]; } tmpStackBuffer[MAX_STACK_ARGS];
    jbyte *tmpBuffer = (jbyte *) &tmpStackBuffer[0];
    void* ffiStackArgs[MAX_STACK_ARGS], **ffiArgs = &ffiStackArgs[0];
    Array stackArrays[MAX_STACK_OBJECTS], *arrays = &stackArrays[0];
    StackAllocator alloc;
    unsigned int i, arrayCount = 0;

#if defined(USE_RAW)
    if (unlikely((unsigned int) ctx->rawParameterSize > sizeof(tmpStackBuffer))) {
        tmpBuffer = alloca_aligned(ctx->rawParameterSize, MIN_ALIGN);
    }
    (*env)->GetByteArrayRegion(env, paramBuffer, 0, ctx->rawParameterSize, tmpBuffer);
#else
    if (unlikely(ctx->cif.nargs > MAX_STACK_ARGS)) {
        ffiArgs = alloca_aligned(ctx->cif.nargs * sizeof(void *), MIN_ALIGN);
        tmpBuffer = alloca_aligned(ctx->cif.nargs * PARAM_SIZE, MIN_ALIGN);
    }
    for (i = 0; i < ctx->cif.nargs; ++i) {
        ffiArgs[i] = &tmpBuffer[i * PARAM_SIZE];
    }
    (*env)->GetByteArrayRegion(env, paramBuffer, 0, ctx->cif.nargs * PARAM_SIZE, tmpBuffer);
#endif
    
    if (unlikely(objectCount > MAX_STACK_OBJECTS)) {
        arrays = alloca_aligned(objectCount * sizeof(Array), MIN_ALIGN);
    }
    initStackAllocator(&alloc);
    for (i = 0; i < (unsigned int) objectCount; ++i) {
        int type = infoBuffer[i * 3];
        jsize offset = infoBuffer[(i * 3) + 1];
        jsize length = infoBuffer[(i * 3) + 2];
        jobject object = objectBuffer[i];
        int idx = (type & com_kenai_jffi_ObjectBuffer_INDEX_MASK) >> com_kenai_jffi_ObjectBuffer_INDEX_SHIFT;
        void* ptr;

        switch (type & com_kenai_jffi_ObjectBuffer_TYPE_MASK & ~com_kenai_jffi_ObjectBuffer_PRIM_MASK) {
            case com_kenai_jffi_ObjectBuffer_ARRAY:
                ptr = jffi_getArray(env, object, offset, length, type, &alloc, &arrays[arrayCount]);
                if (unlikely(ptr == NULL)) {
                    throwException(env, NullPointer, "Could not allocate array");
                    goto cleanup;
                }
                ++arrayCount;
                break;
            case com_kenai_jffi_ObjectBuffer_BUFFER:
                ptr = (*env)->GetDirectBufferAddress(env, object);
                if (unlikely(ptr == NULL)) {
                    throwException(env, NullPointer, "Could not get direct Buffer address");
                    goto cleanup;
                }
                ptr = ((char *) ptr + offset);
                break;

            case com_kenai_jffi_ObjectBuffer_JNI:
                switch (type & com_kenai_jffi_ObjectBuffer_TYPE_MASK) {
                    case com_kenai_jffi_ObjectBuffer_JNIENV:
                        ptr = env;
                        break;
                    case com_kenai_jffi_ObjectBuffer_JNIOBJECT:
                        ptr = (void *) object;
                        break;
                    default:
                        throwException(env, IllegalArgument, "Unsupported object type: %#x",
                            type & com_kenai_jffi_ObjectBuffer_TYPE_MASK);
                        goto cleanup;
                }
                
                break;
            default:
                throwException(env, IllegalArgument, "Unsupported object type: %#x", 
                        type & com_kenai_jffi_ObjectBuffer_TYPE_MASK);
                goto cleanup;
        }

#if defined(USE_RAW)
        *((void **)(tmpBuffer + ctx->rawParamOffsets[idx])) = ptr;
#else
        if (unlikely(ctx->cif.arg_types[idx]->type == FFI_TYPE_STRUCT)) {
            ffiArgs[idx] = ptr;
        } else {
            *((void **) ffiArgs[idx]) = ptr;
        }
#endif
    }
#if defined(USE_RAW)
    //
    // Special case for struct return values - unroll into a ptr array and
    // use ffi_call, since ffi_raw_call with struct return values is undocumented.
    //
    if (unlikely(ctx->cif.rtype->type == FFI_TYPE_STRUCT)) {
        ffiArgs = alloca(ctx->cif.nargs * sizeof(void *));
        ffi_raw_to_ptrarray(&ctx->cif, (ffi_raw *) tmpBuffer, ffiArgs);
        ffi_call(&ctx->cif, FFI_FN(ctx->function), retval, ffiArgs);
    } else {
        ffi_raw_call(&ctx->cif, FFI_FN(ctx->function), retval, (ffi_raw *) tmpBuffer);
    }
#else
    ffi_call(&ctx->cif, FFI_FN(ctx->function), retval, ffiArgs);
#endif
    SAVE_ERRNO(ctx);
cleanup:
    /* Release any array backing memory */
    for (i = 0; i < arrayCount; ++i) {
        if (!arrays[i].stack || arrays[i].mode != JNI_ABORT) {
            //printf("releasing array=%p\n", arrays[i].result);
            (*arrays[i].release)(env, &arrays[i]);
        }
    }
}

static void
invokeArrayWithObjects(JNIEnv* env, jlong ctxAddress, jbyteArray paramBuffer,
        jint objectCount, jintArray objectInfo, jobjectArray objectArray, void* retval)
{
    jint stackInfoBuffer[MAX_STACK_OBJECTS * 3], *infoBuffer = &stackInfoBuffer[0];
    jobject stackObjectBuffer[MAX_STACK_OBJECTS], *objectBuffer = &stackObjectBuffer[0];
    int i;
    if (unlikely(objectCount > MAX_STACK_OBJECTS)) {
        infoBuffer = alloca(objectCount * sizeof(jint) * 3);
        objectBuffer = alloca(objectCount * sizeof(jobject));
    }
    (*env)->GetIntArrayRegion(env, objectInfo, 0, objectCount * 3, infoBuffer);
    for (i = 0; i < objectCount; ++i) {
        objectBuffer[i] = (*env)->GetObjectArrayElement(env, objectArray, i);
    }
    invokeArrayWithObjects_(env, ctxAddress, paramBuffer, objectCount, infoBuffer, objectBuffer, retval);
}

/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayWithObjectsInt32
 * Signature: (J[B[I[Ljava/lang/Object;)I
 */
JNIEXPORT jint JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayWithObjectsInt32(JNIEnv* env, jobject self,
        jlong ctxAddress, jbyteArray paramBuffer, jint objectCount, jintArray objectInfo, jobjectArray objectArray)
{
    FFIValue retval;
    invokeArrayWithObjects(env, ctxAddress, paramBuffer, objectCount, objectInfo, objectArray, &retval);
    return_int(retval);
}

/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayO1Int32
 * Signature: (J[BILjava/lang/Object;I)I
 */
JNIEXPORT jint JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayO1Int32(JNIEnv* env, jobject self, jlong ctxAddress,
        jbyteArray paramBuffer, jobject o1, jint o1info, jint o1off, jint o1len)
{
    FFIValue retval;
    jint info[] = { o1info, o1off, o1len };
    jobject objects[] = { o1 };
    invokeArrayWithObjects_(env, ctxAddress, paramBuffer, 1, info, objects, &retval);
    return_int(retval);
}

/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayO2Int32
 * Signature: (J[BLjava/lang/Object;IIILjava/lang/Object;III)I
 */
JNIEXPORT jint JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayO2Int32(JNIEnv* env, jobject self, jlong ctxAddress,
        jbyteArray paramBuffer, jobject o1, jint o1info, jint o1off, jint o1len,
        jobject o2, jint o2info, jint o2off, jint o2len)
{
    FFIValue retval;
    jint info[] = { o1info, o1off, o1len, o2info, o2off, o2len };
    jobject objects[] = { o1, o2 };
    invokeArrayWithObjects_(env, ctxAddress, paramBuffer, 2, info, objects, &retval);
    return_int(retval);
}

/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayWithObjectsInt64
 * Signature: (J[BI[I[Ljava/lang/Object;)J
 */
JNIEXPORT jlong JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayWithObjectsInt64(JNIEnv* env, jobject self,
        jlong ctxAddress, jbyteArray paramBuffer, jint objectCount, jintArray objectInfo, jobjectArray objectArray)
{
    FFIValue retval;
    invokeArrayWithObjects(env, ctxAddress, paramBuffer, objectCount, objectInfo, objectArray, &retval);
    return retval.s64;
}

/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayO1Int64
 * Signature: (J[BLjava/lang/Object;III)J
 */
JNIEXPORT jlong JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayO1Int64(JNIEnv* env, jobject self, jlong ctxAddress,
        jbyteArray paramBuffer, jobject o1, jint o1info, jint o1off, jint o1len)
{
    FFIValue retval;
    jint info[] = { o1info, o1off, o1len };
    jobject objects[] = { o1 };

    invokeArrayWithObjects_(env, ctxAddress, paramBuffer, 1, info, objects, &retval);

    return retval.j;
}

/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayO2Int64
 * Signature: (J[BLjava/lang/Object;IIILjava/lang/Object;III)J
 */
JNIEXPORT jlong JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayO2Int64(JNIEnv* env, jobject self, jlong ctxAddress,
        jbyteArray paramBuffer, jobject o1, jint o1info, jint o1off, jint o1len,
        jobject o2, jint o2info, jint o2off, jint o2len)
{
    FFIValue retval;
    jint info[] = { o1info, o1off, o1len, o2info, o2off, o2len };
    jobject objects[] = { o1, o2 };

    invokeArrayWithObjects_(env, ctxAddress, paramBuffer, 2, info, objects, &retval);

    return retval.j;
}

/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayWithObjectsFloat
 * Signature: (J[BI[I[Ljava/lang/Object;)F
 */
JNIEXPORT jfloat JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayWithObjectsFloat(JNIEnv* env, jobject self,
        jlong ctxAddress, jbyteArray paramBuffer, jint objectCount, jintArray objectInfo, jobjectArray objectArray)
{
    FFIValue retval;
    invokeArrayWithObjects(env, ctxAddress, paramBuffer, objectCount, objectInfo, objectArray, &retval);
    return retval.f;
}

/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayWithObjectsDouble
 * Signature: (J[BI[I[Ljava/lang/Object;)D
 */
JNIEXPORT jdouble JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayWithObjectsDouble(JNIEnv* env, jobject self,
        jlong ctxAddress, jbyteArray paramBuffer, jint objectCount, jintArray objectInfo, jobjectArray objectArray)
{
    FFIValue retval;
    invokeArrayWithObjects(env, ctxAddress, paramBuffer, objectCount, objectInfo, objectArray, &retval);
    return retval.d;
}

/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayWithObjectsReturnStruct
 * Signature: (J[BI[I[Ljava/lang/Object;[BI)V
 */
JNIEXPORT void JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayWithObjectsReturnStruct(JNIEnv* env, jobject self,
       jlong ctxAddress, jbyteArray paramBuffer, jint objectCount, jintArray objectInfo,
       jobjectArray objectArray, jbyteArray returnBuffer, jint returnBufferOffset)
{
    Function* ctx = (Function *) j2p(ctxAddress);
    caddr_t mem = malloc(ctx->cif.rtype->size + ctx->cif.rtype->alignment - 1);
    jbyte* retval = (jbyte *) ((((uintptr_t) mem) & ~(ctx->cif.rtype->alignment - 1)) + ctx->cif.rtype->alignment);

    invokeArrayWithObjects(env, ctxAddress, paramBuffer, objectCount, objectInfo, objectArray, retval);
    (*env)->SetByteArrayRegion(env, returnBuffer, returnBufferOffset, ctx->cif.rtype->size, retval);
    free(mem);
}

/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokePointerParameterArray
 * Signature: (JJ[J)V
 */
JNIEXPORT void JNICALL
Java_com_kenai_jffi_Foreign_invokePointerParameterArray(JNIEnv *env, jobject self, jlong ctxAddress,
        jlong returnBuffer, jlongArray parameterArray)
{
    Function* ctx = (Function *) j2p(ctxAddress);
    int parameterCount;
    jlong* params = NULL;
    void** ffiArgs = NULL;
    int i;

    if (unlikely(ctxAddress == 0LL)) {
        throwException(env, NullPointer, "context address is null");
        return;
    }

    if (unlikely(returnBuffer == 0LL)) {
        throwException(env, NullPointer, "result buffer is null");
        return;
    }

    if (unlikely(parameterArray == NULL)) {
        throwException(env, NullPointer, "parameter array is null");
        return;
    }

    parameterCount = (*env)->GetArrayLength(env, parameterArray);
    if (parameterCount > 0) {
         params = alloca(parameterCount * sizeof(jlong));
         ffiArgs = alloca(parameterCount * sizeof(void *));
        (*env)->GetLongArrayRegion(env, parameterArray, 0, parameterCount, params);
        for (i = 0; i < parameterCount; ++i) {
            ffiArgs[i] = j2p(params[i]);
        }
    }
    
    ffi_call(&ctx->cif, FFI_FN(ctx->function), j2p(returnBuffer), ffiArgs);
}
