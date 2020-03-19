/*******************************<GINKGO LICENSE>******************************
Copyright (c) 2017-2020, the Ginkgo authors
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
******************************<GINKGO LICENSE>*******************************/

// Copy-Pasted code from MAGMA:

//#if (CUDA_VERSION >= 7000) // only for cuda>6000

/*
constexpr int MaxBlockSize = 32;


template <int block_size>
__device__ void
magma_zlowerisai_regs_inv_kernel(
magma_int_t num_rows,
const magma_index_t * __restrict__ Arow,
const magma_index_t * __restrict__ Acol,
const magmaDoubleComplex * __restrict__ Aval,
magma_index_t *Mrow,
magma_index_t *Mcol,
magmaDoubleComplex *Mval )
{
#if (defined( REAL ) && ( __CUDA_ARCH__ >= 300 ))
    int tid = threadIdx.x;
    int row = gridDim.x*blockIdx.y*blockDim.y + blockIdx.x*blockDim.y +
threadIdx.y;

    if( tid >= block_size )
        return;

    if( row >= num_rows )
        return;

    // only if within the size
    int mstart = Mrow[ row ];
    int mlim = Mrow[ row ]-1;

    // NOTE: `dA` is not in registers since it is accessed `dA[idx]` below,
    //       where idx cannot be known at compile time
    //       ---> `dA` is stored in local memory

    magmaDoubleComplex rB;                // registers for trsv
    magmaDoubleComplex dA[ block_size ];  // registers for trisystem
    magmaDoubleComplex rA;

    // set dA to 0
    #pragma unroll
    for( int j = 0; j < block_size; j++ ){
        dA[ j ] = MAGMA_Z_ZERO;
    }

    // generate the triangular systems
    int t = Mcol[ mstart + tid ];
    int k = Arow[ t+1 ] - 1;
    int alim = Arow[ t ]-1;
    int l = Mrow[ row+1 ]-1;
    int idx = block_size-1;
    while( k > alim && l > mlim  ){ // stop once this column is done
        int mcol =  Mcol[ l ];
        int acol = Acol[k];
        if( mcol == acol ){ //match
            dA[ idx ] = Aval[ k ];
            k--;
            l--;
            idx--;
        } else if( acol > mcol ){// need to check next element
            k--;
        } else { // element does not exist, i.e. l < LC.col[k]
            l--; // check next elment in the sparsity pattern
            idx--; // leave this element equal zero
        }
    }

    // second: solve the triangular systems - in registers
    // we know how RHS looks like
    rB = ( tid == 0 ) ? MAGMA_Z_ONE : MAGMA_Z_ZERO;

        // Triangular solve in regs.
    #pragma unroll
    for (int k = 0; k < block_size; k++)
    {
        rA = dA[ k ];
        if (k%block_size == tid)
            rB /= rA;
        magmaDoubleComplex top = magmablas_zshfl(rB, k%block_size);
        if ( tid > k)
            rB -= (top*rA);
    }

    // Drop B to dev memory - in ISAI preconditioner M
    Mval[ mstart + tid ] = rB;

#endif

}
*/
