/*
 * mlt_multitrack.c -- multitrack service class
 * Copyright (C) 2003-2004 Ushodaya Enterprises Limited
 * Author: Charles Yates <charles.yates@pandora.be>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include "mlt_multitrack.h"
#include "mlt_playlist.h"
#include "mlt_frame.h"

#include <stdio.h>
#include <stdlib.h>

/** Private definition.
*/

struct mlt_multitrack_s
{
	// We're extending producer here
	struct mlt_producer_s parent;
	mlt_producer *list;
	int size;
	int count;
};

/** Forward reference.
*/

static int producer_get_frame( mlt_producer parent, mlt_frame_ptr frame, int index );

/** Constructor.
*/

mlt_multitrack mlt_multitrack_init( )
{
	// Allocate the multitrack object
	mlt_multitrack this = calloc( sizeof( struct mlt_multitrack_s ), 1 );

	if ( this != NULL )
	{
		mlt_producer producer = &this->parent;
		if ( mlt_producer_init( producer, this ) == 0 )
		{
			mlt_properties properties = mlt_multitrack_properties( this );
			producer->get_frame = producer_get_frame;
			mlt_properties_set_data( properties, "multitrack", this, 0, NULL, NULL );
			mlt_properties_set( properties, "log_id", "multitrack" );
			mlt_properties_set( properties, "resource", "<multitrack>" );
		}
		else
		{
			free( this );
			this = NULL;
		}
	}
	
	return this;
}

/** Get the producer associated to this multitrack.
*/

mlt_producer mlt_multitrack_producer( mlt_multitrack this )
{
	return &this->parent;
}

/** Get the service associated this multitrack.
*/

mlt_service mlt_multitrack_service( mlt_multitrack this )
{
	return mlt_producer_service( mlt_multitrack_producer( this ) );
}

/** Get the properties associated this multitrack.
*/

mlt_properties mlt_multitrack_properties( mlt_multitrack this )
{
	return mlt_service_properties( mlt_multitrack_service( this ) );
}

/** Initialise position related information.
*/

void mlt_multitrack_refresh( mlt_multitrack this )
{
	int i = 0;

	// Obtain the properties of this multitrack
	mlt_properties properties = mlt_multitrack_properties( this );

	// We need to ensure that the multitrack reports the longest track as its length
	mlt_position length = 0;

	// We need to ensure that fps are the same on all services
	double fps = 0;
	
	// Obtain stats on all connected services
	for ( i = 0; i < this->count; i ++ )
	{
		// Get the producer from this index
		mlt_producer producer = this->list[ i ];

		// If it's allocated then, update our stats
		if ( producer != NULL )
		{
			// If we have more than 1 track, we must be in continue mode
			if ( this->count > 1 )
				mlt_properties_set( mlt_producer_properties( producer ), "eof", "continue" );
			
			// Determine the longest length
			length = mlt_producer_get_playtime( producer ) > length ? mlt_producer_get_playtime( producer ) : length;
			
			// Handle fps
			if ( fps == 0 )
			{
				// This is the first producer, so it controls the fps
				fps = mlt_producer_get_fps( producer );
			}
			else if ( fps != mlt_producer_get_fps( producer ) )
			{
				// Generate a warning for now - the following attempt to fix may fail
				fprintf( stderr, "Warning: fps mismatch on track %d\n", i );

				// It should be safe to impose fps on an image producer, but not necessarily safe for video
				mlt_properties_set_double( mlt_producer_properties( producer ), "fps", fps );
			}
		}
	}

	// Update multitrack properties now - we'll not destroy the in point here
	mlt_properties_set_position( properties, "length", length );
	mlt_properties_set_position( properties, "out", length - 1 );
	mlt_properties_set_double( properties, "fps", fps );
}

/** Connect a producer to a given track.

  	Note that any producer can be connected here, but see special case treatment
	of playlist in clip point determination below.
*/

int mlt_multitrack_connect( mlt_multitrack this, mlt_producer producer, int track )
{
	// Connect to the producer to ourselves at the specified track
	int result = mlt_service_connect_producer( mlt_multitrack_service( this ), mlt_producer_service( producer ), track );

	if ( result == 0 )
	{
		// Resize the producer list if need be
		if ( track >= this->size )
		{
			int i;
			this->list = realloc( this->list, ( track + 10 ) * sizeof( mlt_producer ) );
			for ( i = this->size; i < track + 10; i ++ )
				this->list[ i ] = NULL;
			this->size = track + 10;
		}
		
		// Assign the track in our list here
		this->list[ track ] = producer;
		
		// Increment the track count if need be
		if ( track >= this->count )
			this->count = track + 1;
			
		// Refresh our stats
		mlt_multitrack_refresh( this );
	}

	return result;
}

/** Get the number of tracks.
*/

int mlt_multitrack_count( mlt_multitrack this )
{
	return this->count;	
}

/** Get an individual track as a producer.
*/

mlt_producer mlt_multitrack_track( mlt_multitrack this, int track )
{
	mlt_producer producer = NULL;
	
	if ( this->list != NULL && track < this->count )
		producer = this->list[ track ];

	return producer;
}


/** Determine the clip point.

  	Special case here: a 'producer' has no concept of multiple clips - only the 
	playlist and multitrack producers have clip functionality. Further to that a 
	multitrack determines clip information from any connected tracks that happen 
	to be playlists.

	Additionally, it must locate clips in the correct order, for example, consider
	the following track arrangement:

	playlist1 |0.0     |b0.0      |0.1          |0.1         |0.2           |
	playlist2 |b1.0  |1.0           |b1.1     |1.1             |

	Note - b clips represent blanks. They are also reported as clip positions.

	When extracting clip positions from these playlists, we should get a sequence of:

	0.0, 1.0, b0.0, 0.1, b1.1, 1.1, 0.1, 0.2, [out of playlist2], [out of playlist1]
*/

mlt_position mlt_multitrack_clip( mlt_multitrack this, mlt_whence whence, int index )
{
	int first = 1;
	mlt_position position = 0;
	int i = 0;

	// Loop through each of the tracks
	for ( i = 0; i < this->count; i ++ )
	{
		// Get the producer for this track
		mlt_producer producer = this->list[ i ];

		// If it's assigned...
		if ( producer != NULL )
		{
			// Get the properties of this producer
			mlt_properties properties = mlt_producer_properties( producer );

			// Determine if it's a playlist
			mlt_playlist playlist = mlt_properties_get_data( properties, "playlist", NULL );

			// We only consider playlists
			if ( playlist != NULL )
			{
				// Locate the smallest position
				if ( first )
				{
					// First position found
					position = mlt_playlist_clip( playlist, whence, index );
	
					// We're no longer first
					first = 0;
				}
				else
				{
					// Obtain the clip position in this playlist
					//mlt_position position2 = mlt_playlist_clip( playlist, whence, index );

					// If this position is prior to the first, then use it
					//if ( position2 < position )
						//position = position2;
				}
			}
			else
			{
				fprintf( stderr, "track %d isn't a playlist\n", index );
			}
		}
	}

	return position;
}

/** Get frame method.

  	Special case here: The multitrack must be used in a conjunction with a downstream
	tractor-type service, ie:

	Producer1 \
	Producer2 - multitrack - { filters/transitions } - tractor - consumer
	Producer3 /

	The get_frame of a tractor pulls frames from it's connected service on all tracks and 
	will terminate as soon as it receives a test card with a last_track property. The 
	important case here is that the mulitrack does not move to the next frame until all
	tracks have been pulled. 

	Reasoning: In order to seek on a network such as above, the multitrack needs to ensure
	that all producers are positioned on the same frame. It uses the 'last track' logic
	to determine when to move to the next frame.

	Flaw: if a transition is configured to read from a b-track which happens to trigger
	the last frame logic (ie: it's configured incorrectly), then things are going to go
	out of sync.

	See playlist logic too.
*/

static int producer_get_frame( mlt_producer parent, mlt_frame_ptr frame, int index )
{
	// Get the mutiltrack object
	mlt_multitrack this = parent->child;

	// Check if we have a track for this index
	if ( index < this->count && this->list[ index ] != NULL )
	{
		// Get the producer for this track
		mlt_producer producer = this->list[ index ];

		// Obtain the current position
		mlt_position position = mlt_producer_frame( parent );

		// Make sure we're at the same point
		mlt_producer_seek( producer, position );

		// Get the frame from the producer
		mlt_service_get_frame( mlt_producer_service( producer ), frame, 0 );

		// Indicate speed of this producer
		mlt_properties producer_properties = mlt_producer_properties( parent );
		double speed = mlt_properties_get_double( producer_properties, "speed" );
		mlt_properties properties = mlt_frame_properties( *frame );
		mlt_properties_set_double( properties, "speed", speed );
	}
	else
	{
		// Generate a test frame
		*frame = mlt_frame_init( );

		// Update position on the frame we're creating
		mlt_frame_set_position( *frame, mlt_producer_position( parent ) );

		// Move on to the next frame
		if ( index >= this->count )
		{
			// Let tractor know if we've reached the end
			mlt_properties_set_int( mlt_frame_properties( *frame ), "last_track", 1 );

			// Move to the next frame
			mlt_producer_prepare_next( parent );
		}

		// Refresh our stats
		mlt_multitrack_refresh( this );
	}

	return 0;
}

/** Close this instance.
*/

void mlt_multitrack_close( mlt_multitrack this )
{
	// Close the producer
	mlt_producer_close( &this->parent );

	// Free the list
	free( this->list );

	// Free the object
	free( this );
}
