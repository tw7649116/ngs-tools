/*===========================================================================
*
*                            PUBLIC DOMAIN NOTICE
*               National Center for Biotechnology Information
*
*  This software/database is a "United States Government Work" under the
*  terms of the United States Copyright Act.  It was written as part of
*  the author's official duties as a United States Government employee and
*  thus cannot be copyrighted.  This software/database is freely available
*  to the public for use. The National Library of Medicine and the U.S.
*  Government have not placed any restriction on its use or reproduction.
*
*  Although all reasonable efforts have been taken to ensure the accuracy
*  and reliability of the software and data, the NLM and the U.S.
*  Government do not and cannot warrant the performance or results that
*  may be obtained by using this software or data. The NLM and the U.S.
*  Government disclaim all warranties, express or implied, including
*  warranties of performance, merchantability or fitness for any particular
*  purpose.
*
*  Please cite the author in any work or product based on this material.
*
* ===========================================================================
*
*/
package Bio;

import data.CLogger;
import job.JobData;
import ngs.ErrorMsg;
import ngs.Read;
import ngs.ReadCollection;
import ngs.ReadIterator;

public class BioRunDumper extends BioDumper
{
    protected final ReadIterator iter;
    
    @Override public boolean next()
    {
        boolean res = false;
        if ( iter != null )
        {
            try
            {
                res = iter.nextRead();
            }
            catch ( ErrorMsg msg )
            {
                CLogger.logfmt( "job >%s< error calling nextRead(): %s",
                        name, msg.toString() );
            }
        }
        return res;
    }

    public BioRunDumper( final ReadCollection run,
                         final BioFormatter fmt,
                         final JobData job,
                         final long start ) throws ErrorMsg
    {
        super( fmt, job.get_short_source() );
        
        int read_type_filter;
        if ( job.get_use_bio_read_type() )
            read_type_filter = job.get_bio_read_type().to_read_type();
        else
            read_type_filter = Read.all;
        
        long iter_start = start;
        long iter_count = run.getReadCount();
        if ( job.get_use_row_filter() )
        {
            long filter_start = job.get_start_row();
            long filter_count = job.get_row_count();
            long filter_end = filter_start + filter_count - 1;

            if ( iter_start < filter_start )
                iter_start = filter_start;
            else if ( iter_start > filter_end )
            {
                CLogger.logfmt( "job >%s< already reached end of filter-row-range", name );
                iter_count = 0;
            }
            if ( iter_count > 0 )
                iter_count = filter_end - iter_start + 1;
        }
        iter = run.getReadRange( iter_start, iter_count, read_type_filter );
    }
}
