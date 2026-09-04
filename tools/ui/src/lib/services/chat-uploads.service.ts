import { API_UPLOADS } from '$lib/constants';
import { apiFetch } from '$lib/utils';

export interface ChatUploadResult {
	id: string;
	mimeType: string;
	name: string;
	path: string;
	size: number;
}

interface UploadResponse {
	id: string;
	mime_type?: string;
	mimeType?: string;
	name: string;
	path: string;
	size: number;
}

/**
 * Uploads a chat attachment to llama-server as a temporary file so MCP
 * tools can open a real path instead of a data URI.
 */
export class ChatUploadsService {
	private static cache = new Map<string, ChatUploadResult>();

	static async upload(name: string, mimeType: string, data: string): Promise<ChatUploadResult> {
		const key = `${name}:${mimeType}:${data.slice(0, 48)}:${data.length}`;
		const cached = this.cache.get(key);

		if (cached) {
			return cached;
		}

		const response = await apiFetch<UploadResponse>(API_UPLOADS.CREATE, {
			body: JSON.stringify({ data, mime_type: mimeType, name }),
			method: 'POST'
		});

		const result: ChatUploadResult = {
			id: response.id,
			mimeType: response.mime_type ?? response.mimeType ?? mimeType,
			name: response.name,
			path: response.path,
			size: response.size
		};

		this.cache.set(key, result);

		return result;
	}
}
